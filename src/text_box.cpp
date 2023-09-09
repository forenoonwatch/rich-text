#include "text_box.hpp"

#include "font_cache.hpp"
#include "image.hpp"
#include "pipeline.hpp"
#include "text_atlas.hpp"
#include "msdf_text_atlas.hpp"
#include "paragraph_layout.hpp"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

static constexpr const bool g_useMSDF = false;
static constexpr const bool g_showGlyphOutlines = false;

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

		if (g_showGlyphOutlines && rect.pipeline != PipelineIndex::OUTLINE) {
			if (pipelineIndex != PipelineIndex::OUTLINE) {
				pipelineIndex = PipelineIndex::OUTLINE;
				pPipeline = &g_pipelines[static_cast<size_t>(pipelineIndex)];
				pPipeline->bind();
				pPipeline->set_uniform_float2(0, invScreenSize);
			}

			Color outlineColor{0.f, 0.5f, 0.f, 1.f};
			pPipeline->set_uniform_float4(1, extents);
			pPipeline->set_uniform_float4(2, rect.texCoords);
			pPipeline->set_uniform_float4(3, reinterpret_cast<const float*>(&outlineColor));
			pPipeline->draw();
		}
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
	Text::LayoutInfo layoutInfo{};
	Text::build_line_layout_info(textInfo, m_size[0], layoutInfo);

	// Add Stroke Glyphs
	Text::for_each_glyph(layoutInfo, m_size[0], [&](auto glyphID, auto charIndex, auto* position, auto& font,
			auto lineX, auto lineY) {
		auto stroke = textInfo.strokeRuns.get_value(charIndex);

		if (stroke.color.a > 0.f) {
			auto pX = position[0];
			auto pY = position[1];

			float offset[2]{};
			float texCoordExtents[4]{};
			float glyphSize[2]{};
			bool strokeHasColor{};
			auto* pGlyphImage = g_useMSDF ? g_msdfTextAtlas->get_stroke_info(font, glyphID, stroke.thickness,
					stroke.joins, texCoordExtents, glyphSize, offset, strokeHasColor)
					: g_textAtlas->get_stroke_info(font, glyphID, stroke.thickness, stroke.joins,
					texCoordExtents, glyphSize, offset, strokeHasColor);

			m_textRects.push_back({
				.x = lineX + pX + offset[0],
				.y = lineY + pY + offset[1],
				.width = glyphSize[0],
				.height = glyphSize[1],
				.texCoords = {texCoordExtents[0], texCoordExtents[1],
						texCoordExtents[2], texCoordExtents[3]},
				.texture = pGlyphImage,
				.color = stroke.color,
				.pipeline = g_useMSDF ? PipelineIndex::MSDF : PipelineIndex::RECT,
			});
		}
	});

	// Add Main Glyphs
	Text::for_each_glyph(layoutInfo, m_size[0], [&](auto glyphID, auto charIndex, auto* position, auto& font,
			auto lineX, auto lineY) {
		auto pX = position[0];
		auto pY = position[1];

		float offset[2]{};
		float texCoordExtents[4]{};
		float glyphSize[2]{};
		bool glyphHasColor{};

		auto* pGlyphImage = g_useMSDF ? g_msdfTextAtlas->get_glyph_info(font, glyphID, texCoordExtents,
				glyphSize, offset, glyphHasColor)
				: g_textAtlas->get_glyph_info(font, glyphID, texCoordExtents, glyphSize, offset,
				glyphHasColor);
		auto textColor = glyphHasColor ? Color{1.f, 1.f, 1.f, 1.f} : textInfo.colorRuns.get_value(charIndex);

		if (textInfo.strikethroughRuns.get_value(charIndex)) {
			auto height = font.get_strikethrough_thickness() + 0.5f;
			m_textRects.push_back({
				.x = lineX + pX + offset[0],
				.y = lineY + pY + font.get_strikethrough_position(),
				.width = glyphSize[0],
				.height = height,
				.texture = g_textAtlas->get_default_texture(),
				.color = textColor,
				.pipeline = PipelineIndex::RECT,
			});
		}

		if (textInfo.underlineRuns.get_value(charIndex)) {
			auto height = font.get_underline_thickness() + 0.5f;
			m_textRects.push_back({
				.x = lineX + pX + offset[0],
				.y = lineY + pY + font.get_underline_position(),
				.width = glyphSize[0],
				.height = height,
				.texture = g_textAtlas->get_default_texture(),
				.color = textColor,
				.pipeline = PipelineIndex::RECT,
			});
		}

		m_textRects.push_back({
			.x = lineX + pX + offset[0],
			.y = lineY + pY + offset[1],
			.width = glyphSize[0],
			.height = glyphSize[1],
			.texCoords = {texCoordExtents[0], texCoordExtents[1], texCoordExtents[2], texCoordExtents[3]},
			.texture = pGlyphImage,
			.color = std::move(textColor),
			.pipeline = g_useMSDF ? PipelineIndex::MSDF : PipelineIndex::RECT,
		});
	});
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

