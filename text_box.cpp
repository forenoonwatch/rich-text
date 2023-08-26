#include "text_box.hpp"

#include "font.hpp"
#include "font_cache.hpp"
#include "rich_text.hpp"

#include <layout/ParagraphLayout.h>
#include <layout/RunArrays.h>

void TextBox::render(Bitmap& target) {
	for (auto& rect : m_textRects) {
		target.blit_alpha(rect.texture, static_cast<int32_t>(rect.x), static_cast<int32_t>(rect.y), rect.color);
	}
}

void TextBox::recalc_text() {
	m_textRects.clear();

	if (!m_font) {
		return;
	}

	auto runs = m_richText ? RichText::parse(m_text, m_contentText, *m_font, m_textColor)
			: RichText::make_default_runs(m_text, m_contentText, *m_font, m_textColor);

	if (m_contentText.empty()) {
		return;
	}

	create_text_rects(runs);
}

void TextBox::create_text_rects(RichText::Result& textInfo) {
	auto** ppFonts = const_cast<const Font**>(textInfo.fontRuns.get_values());
	icu::FontRuns fontRuns(reinterpret_cast<const icu::LEFontInstance**>(ppFonts),
			textInfo.fontRuns.get_limits(), textInfo.fontRuns.get_value_count());

	LEErrorCode err{};
	icu::ParagraphLayout pl(textInfo.str.getBuffer(), textInfo.str.length(), &fontRuns, nullptr, nullptr,
			nullptr, UBIDI_DEFAULT_LTR, false, err);
	auto paragraphLevel = pl.getParagraphLevel();

	float lineX = 0.f;
	float lineY = m_font->get_baseline();

	float lineWidth = m_size[0];
	float lineHeight = m_font->get_line_height();
	
	while (auto* line = pl.nextLine(lineWidth)) {
		if (paragraphLevel == UBIDI_RTL) {
			auto lastX = line->getWidth();
			lineX = lineWidth - lastX;
		}

		for (le_int32 runID = 0; runID < line->countRuns(); ++runID) {
			auto* run = line->getVisualRun(runID);
			auto* posData = run->getPositions();
			auto* pFont = static_cast<const Font*>(run->getFont());
			auto* pGlyphs = run->getGlyphs();
			auto* pGlyphChars = run->getGlyphToCharMap();

			for (le_int32 i = 0; i < run->getGlyphCount(); ++i) {
				auto pX = posData[2 * i];
				auto pY = posData[2 * i + 1];
				float offset[2]{};
				auto glyphBitmap = pFont->get_glyph(LE_GET_GLYPH(pGlyphs[i]), offset);

				m_textRects.push_back({
					.x = lineX + pX + offset[0],
					.y = lineY + pY + offset[1],
					.texture = std::move(glyphBitmap),
					.color = textInfo.colorRuns.get_value(pGlyphChars[i]),
				});
			}
		}

		delete line;
		lineY += lineHeight;
	}
}

// Setters

void TextBox::set_font(Font* font) {
	m_font = font;
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

