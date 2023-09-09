#include "text_box.hpp"

#include "font.hpp"
#include "font_cache.hpp"
#include "rich_text.hpp"
#include "image.hpp"
#include "pipeline.hpp"
#include "text_atlas.hpp"
#include "msdf_text_atlas.hpp"

#include <layout/ParagraphLayout.h>
#include <layout/RunArrays.h>

#include <unicode/utext.h>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

static constexpr const bool g_useMSDF = false;

static constexpr const UChar32 CH_LF = 0x000A;
static constexpr const UChar32 CH_CR = 0x000D;
static constexpr const UChar32 CH_LSEP = 0x2028;
static constexpr const UChar32 CH_PSEP = 0x2029;

static TextBox* g_focusedTextBox = nullptr;

TextBox* TextBox::get_focused_text_box() {
	return g_focusedTextBox;
}

TextBox::~TextBox() {
	release_focus();
}

bool TextBox::handle_mouse_button(int button, double mouseX, double mouseY) {
	bool mouseInside = is_mouse_inside(mouseX, mouseY);

	if (g_focusedTextBox == this) {
		if (is_mouse_inside(mouseX, mouseY)) {
			// FIXME: click inside focused text box - move cursor or something
		}
		else {
			release_focus();
		}
	}
	else {
		capture_focus();
	}

	return mouseInside;
}

bool TextBox::handle_key_press(int key, int action, int mods) {
	return g_focusedTextBox == this;
}

bool TextBox::handle_text_input(unsigned codepoint) {
	return g_focusedTextBox == this;
}

void TextBox::capture_focus() {
	if (g_focusedTextBox == this) {
		return;
	}
	else if (g_focusedTextBox) {
		g_focusedTextBox->release_focus();
	}

	g_focusedTextBox = this;

	recalc_text_internal(false);
}

void TextBox::release_focus() {
	if (g_focusedTextBox != this) {
		return;
	}

	g_focusedTextBox = nullptr;

	recalc_text();
}

void TextBox::render(const float* invScreenSize) {
	PipelineIndex pipelineIndex{PipelineIndex::INVALID};
	Pipeline* pPipeline = nullptr;

	for (auto& rect : m_textRects) {
		if (!rect.texture) {
			continue;
		}

		if (rect.pipeline != pipelineIndex) {
			pipelineIndex = rect.pipeline;
			pPipeline = &g_pipelines[static_cast<size_t>(pipelineIndex)];
			pPipeline->bind();
			pPipeline->set_uniform_float2(0, invScreenSize);
		}

		rect.texture->bind();
		float extents[] = {m_position[0] + rect.x, m_position[1] + rect.y, rect.width, rect.height}; 
		pPipeline->set_uniform_float4(1, extents);
		pPipeline->set_uniform_float4(2, rect.texCoords);
		pPipeline->set_uniform_float4(3, reinterpret_cast<const float*>(&rect.color));
		pPipeline->draw();
	}
}

bool TextBox::is_mouse_inside(double mouseX, double mouseY) const {
	return mouseX >= m_position[0] && mouseY >= m_position[1] && mouseX - m_position[0] <= m_size[0]
			&& mouseY - m_position[1] <= m_size[1];
}

// Private Methods

void TextBox::recalc_text() {
	recalc_text_internal(m_richText);
}

void TextBox::recalc_text_internal(bool richText) {
	m_textRects.clear();

	if (!m_font) {
		return;
	}

	RichText::StrokeState strokeState{};
	auto runs = richText ? RichText::parse(m_text, m_contentText, m_font, m_textColor, strokeState)
			: RichText::make_default_runs(m_text, m_contentText, m_font, m_textColor, strokeState);

	if (m_contentText.empty()) {
		return;
	}

	create_text_rects(runs);
}

void TextBox::create_text_rects(RichText::Result& textInfo) {
	RichText::TextRuns<const MultiScriptFont*> subsetFontRuns(textInfo.fontRuns.get_value_count());

	auto* start = textInfo.str.getBuffer();
	auto* end = start + textInfo.str.length();
	UText iter UTEXT_INITIALIZER;
	UErrorCode err{};
	utext_openUnicodeString(&iter, &textInfo.str, &err);

	int32_t byteIndex = 0;

	std::vector<icu::ParagraphLayout::Line*> lines;
	RichText::TextRuns<int32_t> offsetRunsByLine;

	int32_t maxAscent = 0;
	int32_t maxDescent = 0;
	UBiDiLevel paragraphLevel = UBIDI_DEFAULT_LTR;

	for (;;) {
		auto idx = UTEXT_GETNATIVEINDEX(&iter);
		auto c = UTEXT_NEXT32(&iter);

		if (c == U_SENTINEL || c == CH_LF || c == CH_CR || c == CH_LSEP || c == CH_PSEP) {
			if (idx != byteIndex) {
				auto byteCount = idx - byteIndex;

				subsetFontRuns.clear();
				textInfo.fontRuns.get_runs_subset(byteIndex, byteCount, subsetFontRuns);

				LEErrorCode err{};
				auto** ppFonts = const_cast<const MultiScriptFont**>(subsetFontRuns.get_values());
				icu::FontRuns fontRuns(reinterpret_cast<const icu::LEFontInstance**>(ppFonts),
						subsetFontRuns.get_limits(), subsetFontRuns.get_value_count());
				icu::ParagraphLayout pl(textInfo.str.getBuffer() + byteIndex, byteCount, &fontRuns, nullptr,
						nullptr, nullptr, paragraphLevel, false, err);

				if (paragraphLevel == UBIDI_DEFAULT_LTR) {
					paragraphLevel = pl.getParagraphLevel();
				}

				auto ascent = pl.getAscent();
				auto descent = pl.getDescent();

				if (ascent > maxAscent) {
					maxAscent = ascent;
				}

				if (descent > maxDescent) {
					maxDescent = descent;
				}

				while (auto* pLine = pl.nextLine(m_size[0])) {
					lines.emplace_back(pLine);
				}

				offsetRunsByLine.add(static_cast<int32_t>(lines.size()), byteIndex);
			}
			else {
				lines.emplace_back(nullptr);
			}

			if (c == U_SENTINEL) {
				break;
			}
			else if (c == CH_CR && UTEXT_CURRENT32(&iter) == CH_LF) {
				UTEXT_NEXT32(&iter);
			}

			byteIndex = UTEXT_GETNATIVEINDEX(&iter);
		}
	}

	auto lineY = static_cast<float>(maxAscent);
	auto lineHeight = static_cast<float>(maxDescent + maxAscent);

	for (size_t lineNumber = 0; lineNumber < lines.size(); ++lineNumber) {
		if (!lines[lineNumber]) {
			lineY += lineHeight;
			continue;
		}

		auto charOffset = offsetRunsByLine.get_value(static_cast<int32_t>(lineNumber));

		float lineX = 0.f;

		if (paragraphLevel == UBIDI_RTL) {
			auto lastX = lines[lineNumber]->getWidth();
			lineX = m_size[0] - lastX;
		}

		for (le_int32 runID = 0; runID < lines[lineNumber]->countRuns(); ++runID) {
			auto* run = lines[lineNumber]->getVisualRun(runID);
			auto* posData = run->getPositions();
			auto* pFont = static_cast<const Font*>(run->getFont());
			auto* pGlyphs = run->getGlyphs();
			auto* pGlyphChars = run->getGlyphToCharMap();

			for (le_int32 i = 0; i < run->getGlyphCount(); ++i) {
				if (pGlyphs[i] == 0xFFFFu || pGlyphs[i] == 0xFFFEu) {
					continue;
				}

				auto globalCharIndex = pGlyphChars[i] + charOffset;
				auto stroke = textInfo.strokeRuns.get_value(globalCharIndex);

				if (stroke.color.a > 0.f) {
					auto pX = posData[2 * i];
					auto pY = posData[2 * i + 1];

					float strokeOffset[2]{};
					float strokeTexCoordExtents[4]{};
					float strokeSize[2]{};
					bool strokeHasColor{};
					auto* pGlyphImage = g_useMSDF ? g_msdfTextAtlas->get_stroke_info(*pFont, pGlyphs[i],
							stroke.thickness, stroke.joins, strokeTexCoordExtents, strokeSize, strokeOffset,
							strokeHasColor)
							: g_textAtlas->get_stroke_info(*pFont, pGlyphs[i], stroke.thickness,
							stroke.joins, strokeTexCoordExtents, strokeSize, strokeOffset, strokeHasColor);

					m_textRects.push_back({
						.x = lineX + pX + strokeOffset[0],
						.y = lineY + pY + strokeOffset[1],
						.width = strokeSize[0],
						.height = strokeSize[1],
						.texCoords = {strokeTexCoordExtents[0], strokeTexCoordExtents[1],
								strokeTexCoordExtents[2], strokeTexCoordExtents[3]},
						.texture = pGlyphImage,
						.color = stroke.color,
						.pipeline = g_useMSDF ? PipelineIndex::MSDF : PipelineIndex::RECT,
					});
				}
			}

			for (le_int32 i = 0; i < run->getGlyphCount(); ++i) {
				if (pGlyphs[i] == 0xFFFFu || pGlyphs[i] == 0xFFFEu) {
					continue;
				}

				auto pX = posData[2 * i];
				auto pY = posData[2 * i + 1];
				auto globalCharIndex = pGlyphChars[i] + charOffset;
				float glyphOffset[2]{};
				float glyphTexCoordExtents[4]{};
				float glyphSize[2]{};
				bool glyphHasColor{};
				auto* pGlyphImage = g_useMSDF ? g_msdfTextAtlas->get_glyph_info(*pFont, pGlyphs[i],
						glyphTexCoordExtents, glyphSize, glyphOffset, glyphHasColor)
						: g_textAtlas->get_glyph_info(*pFont, pGlyphs[i], glyphTexCoordExtents,
						glyphSize, glyphOffset, glyphHasColor);
				auto textColor = glyphHasColor ? Color{1.f, 1.f, 1.f, 1.f}
						: textInfo.colorRuns.get_value(globalCharIndex);

				if (textInfo.strikethroughRuns.get_value(globalCharIndex)) {
					auto height = pFont->get_strikethrough_thickness() + 0.5f;
					m_textRects.push_back({
						.x = lineX + pX + glyphOffset[0],
						.y = lineY + pY + pFont->get_strikethrough_position(),
						.width = glyphSize[0],
						.height = height,
						.texture = g_textAtlas->get_default_texture(),
						.color = textColor,
						.pipeline = PipelineIndex::RECT,
					});
				}

				if (textInfo.underlineRuns.get_value(globalCharIndex)) {
					auto height = pFont->get_underline_thickness() + 0.5f;
					m_textRects.push_back({
						.x = lineX + pX + glyphOffset[0],
						.y = lineY + pY + pFont->get_underline_position(),
						.width = glyphSize[0],
						.height = height,
						.texture = g_textAtlas->get_default_texture(),
						.color = textColor,
						.pipeline = PipelineIndex::RECT,
					});
				}

				m_textRects.push_back({
					.x = lineX + pX + glyphOffset[0],
					.y = lineY + pY + glyphOffset[1],
					.width = glyphSize[0],
					.height = glyphSize[1],
					.texCoords = {glyphTexCoordExtents[0], glyphTexCoordExtents[1], glyphTexCoordExtents[2],
							glyphTexCoordExtents[3]},
					.texture = pGlyphImage,
					.color = textColor,
					.pipeline = g_useMSDF ? PipelineIndex::MSDF : PipelineIndex::RECT,
				});
			}
		}

		delete lines[lineNumber];
		lineY += lineHeight;
	}
}

// Setters

void TextBox::set_font(MultiScriptFont font) {
	m_font = std::move(font);
	recalc_text();
}

void TextBox::set_text(std::string text) {
	m_text = std::move(text);
	recalc_text();
}

void TextBox::set_position(float x, float y) {
	m_position[0] = x;
	m_position[1] = y;
	recalc_text();
}

void TextBox::set_size(float width, float height) {
	m_size[0] = width;
	m_size[1] = height;
	recalc_text();
}

void TextBox::set_text_wrapped(bool wrapped) {
	m_textWrapped = wrapped;
	recalc_text();
}

void TextBox::set_rich_text(bool richText) {
	m_richText = richText;
	recalc_text();
}

