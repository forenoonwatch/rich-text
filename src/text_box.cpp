#include "text_box.hpp"

#include "font.hpp"
#include "font_cache.hpp"
#include "rich_text.hpp"

#include <layout/ParagraphLayout.h>
#include <layout/RunArrays.h>

#include <unicode/utext.h>

static constexpr const UChar32 CH_LF = 0x000A;
static constexpr const UChar32 CH_CR = 0x000D;
static constexpr const UChar32 CH_LSEP = 0x2028;
static constexpr const UChar32 CH_PSEP = 0x2029;

void TextBox::render(Bitmap& target) {
	for (auto& rect : m_textRects) {
		target.blit_alpha(rect.texture, static_cast<int32_t>(m_position[0] + rect.x),
				static_cast<int32_t>(m_position[1] + rect.y), rect.color);
	}
}

void TextBox::recalc_text() {
	m_textRects.clear();

	if (!m_font) {
		return;
	}

	auto runs = m_richText ? RichText::parse(m_text, m_contentText, m_font, m_textColor)
			: RichText::make_default_runs(m_text, m_contentText, m_font, m_textColor);

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
				auto pX = posData[2 * i];
				auto pY = posData[2 * i + 1];
				auto globalCharIndex = pGlyphChars[i] + charOffset;
				float glyphOffset[2]{};
				auto [glyphBitmap, hasColor] = pFont->get_glyph(pGlyphs[i], glyphOffset);
				auto textColor = hasColor ? Color{1.f, 1.f, 1.f, 1.f}
						: textInfo.colorRuns.get_value(globalCharIndex);

				if (textInfo.strikethroughRuns.get_value(globalCharIndex)) {
					auto height = static_cast<uint32_t>(pFont->get_strikethrough_thickness() + 0.5f);
					m_textRects.push_back({
						.x = lineX + pX + glyphOffset[0],
						.y = lineY + pY + pFont->get_strikethrough_position(),
						.texture = Bitmap(glyphBitmap.get_width(), height, {1.f, 1.f, 1.f, 1.f}),
						.color = textColor,
					});
				}

				if (textInfo.underlineRuns.get_value(globalCharIndex)) {
					auto height = static_cast<uint32_t>(pFont->get_underline_thickness() + 0.5f);
					m_textRects.push_back({
						.x = lineX + pX + glyphOffset[0],
						.y = lineY + pY + pFont->get_underline_position(),
						.texture = Bitmap(glyphBitmap.get_width(), height, {1.f, 1.f, 1.f, 1.f}),
						.color = textColor,
					});
				}

				m_textRects.push_back({
					.x = lineX + pX + glyphOffset[0],
					.y = lineY + pY + glyphOffset[1],
					.texture = std::move(glyphBitmap),
					.color = textColor,
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

