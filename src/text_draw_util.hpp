#pragma once

#include "font_registry.hpp"
#include "formatting.hpp"
#include "formatting_iterator.hpp"
#include "layout_info.hpp"

namespace Text {

template <typename Visitor>
void draw_text(const LayoutInfo& layout, const FormattingRuns& formatting, float textAreaWidth,
		TextXAlignment textXAlignment, Visitor&& visitor) {
	uint32_t glyphIndex{};
	uint32_t glyphPosIndex{};
	float strikethroughStartPos{};
	float underlineStartPos{};
	auto* glyphPositions = layout.get_glyph_position_data();

	layout.for_each_run(textAreaWidth, textXAlignment, [&](auto lineIndex, auto runIndex, auto lineX,
			auto lineY) {
		auto font = layout.get_run_font(runIndex);
		auto fontData = Text::FontRegistry::get_font_data(font);

		visitor(lineIndex, runIndex);

		Text::FormattingIterator iter(formatting, layout.is_run_rtl(runIndex)
				? layout.get_run_char_end_index(runIndex) : layout.get_run_char_start_index(runIndex));
		underlineStartPos = strikethroughStartPos = glyphPositions[glyphPosIndex];	

		for (auto glyphEndIndex = layout.get_run_glyph_end_index(runIndex); glyphIndex < glyphEndIndex; 
				++glyphIndex, glyphPosIndex += 2) {
			auto pX = glyphPositions[glyphPosIndex];
			auto pY = glyphPositions[glyphPosIndex + 1];
			auto glyphID = layout.get_glyph_id(glyphIndex);
			auto event = iter.advance_to(layout.get_char_index(glyphIndex));
			auto stroke = iter.get_stroke_state();

			// Stroke
			if (stroke.color.a > 0.f) {
				visitor(font, glyphID, lineX + pX, lineY + pY, stroke);
			}

			// Main Glyph
			visitor(font, glyphID, lineX + pX, lineY + pY, iter.get_color());
			
			// Underline
			if ((event & Text::FormattingEvent::UNDERLINE_END) != Text::FormattingEvent::NONE) {
				auto height = fontData.get_underline_thickness() + 0.5f;
				visitor(lineX + underlineStartPos, lineY + fontData.get_underline_position(),
						pX - underlineStartPos, height, iter.get_prev_color());
			}

			if ((event & Text::FormattingEvent::UNDERLINE_BEGIN) != Text::FormattingEvent::NONE) {
				underlineStartPos = pX;
			}

			// Strikethrough
			if ((event & Text::FormattingEvent::STRIKETHROUGH_END) != Text::FormattingEvent::NONE) {
				auto height = fontData.get_strikethrough_thickness() + 0.5f;
				visitor(lineX + strikethroughStartPos, lineY + fontData.get_strikethrough_position(),
						pX - strikethroughStartPos, height, iter.get_prev_color());
			}

			if ((event & Text::FormattingEvent::STRIKETHROUGH_BEGIN) != Text::FormattingEvent::NONE) {
				strikethroughStartPos = pX;
			}
		}

		// Finalize last strikethrough
		if (iter.has_strikethrough()) {
			auto strikethroughEndPos = glyphPositions[glyphPosIndex];
			auto height = fontData.get_strikethrough_thickness() + 0.5f;
			visitor(lineX + strikethroughStartPos, lineY + fontData.get_strikethrough_position(),
					strikethroughEndPos - strikethroughStartPos, height, iter.get_color());
		}

		// Finalize last underline
		if (iter.has_underline()) {
			auto underlineEndPos = glyphPositions[glyphPosIndex];
			auto height = fontData.get_underline_thickness() + 0.5f;
			visitor(lineX + underlineStartPos, lineY + fontData.get_underline_position(),
					underlineEndPos - underlineStartPos, height, iter.get_color());
		}

		glyphPosIndex += 2;
	});
}

}

