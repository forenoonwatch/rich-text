#pragma once

#include "common.hpp"
#include "cursor_position.hpp"
#include "text_alignment.hpp"
#include "font.hpp"
#include "pair.hpp"

#include <type_traits>
#include <vector>

#include <unicode/uversion.h>

U_NAMESPACE_BEGIN

class BreakIterator;

U_NAMESPACE_END

namespace Text {

enum class LayoutInfoFlags : uint8_t {
	NONE = 0,
	// Whether the text direction default should be RTL when no strongly directional characters are detected.
	// Leave unset to default to LTR.
	RIGHT_TO_LEFT = 1, 
	// Whether the configured text direction should override the paragraph base direction, regardless of the
	// presence of strongly-directional scripts.
	OVERRIDE_DIRECTIONALITY = 2, 
	// Whether the text is composed vertically. Leave unset for horizontal text.
	VERTICAL = 4, 
	// Whether the tab width parameter is in pixels. Leave unset for tab width in terms of space-widths.
	TAB_WIDTH_PIXELS = 8, 
};

RICHTEXT_DEFINE_ENUM_BITFLAG_OPERATORS(LayoutInfoFlags)

struct VisualCursorInfo {
	float x;
	float y;
	float height;
	uint32_t lineNumber;
};

/**
 * Represents all the information necessary to display a string of text, or query a string for visual
 * information.
 *
 * INTERNAL:
 * All data is stored in Visual Order. Iterating through the list of positions and glyph indices will emit
 * glyphs from left to right, top to bottom. This means that for all RTL runs, character indices into the
 * source string are in reverse order.
 */
class LayoutInfo {
	public:
		/**
		 * @brief Clears all layout information contained within the object.
		 */
		void clear();
		void reserve_runs(size_t runCount);

		void append_glyph(uint32_t glyphID);
		void append_char_index(uint32_t charIndex);
		void append_glyph_position(float x, float y);
		void append_run(const SingleScriptFont& font, uint32_t charStartIndex, uint32_t charEndIndex,
				bool rightToLeft);
		void append_line(float height, float ascent);
		void append_empty_line(const SingleScriptFont& font, uint32_t charIndex, float height, float ascent);
		void set_run_char_end_offset(size_t runIndex, uint8_t charEndOffset);
		void set_text_start_y(float);

		/**
		 * Calculates the pixel position, height, and line number of the text cursor given the provided
		 * `CursorPosition`.
		 *
		 * @param textWidth The width of the area containing the paragraph
		 * @param textXAlignment The X alignment of the paragraph text
		 * @param cursorPosition The position of the cursor
		 */
		VisualCursorInfo calc_cursor_pixel_pos(float textWidth, XAlignment textXAlignment,
				CursorPosition cursorPosition) const;

		/**
		 * Gets the index of the run containing the cursor position, taking into account cursor affinity based on
		 * the following policy:
		 * Line End Default: Next Run Start
		 * LTR-RTL Default: Curr Run End
		 * RTL-LTR Default: Next Run Start
		 *
		 * @param cursorPosition The position of the cursor
		 * @param outLineNumber The line number containing the cursor
		 */
		size_t get_run_containing_cursor(CursorPosition cursorPosition, size_t& outLineNumber) const;

		/**
		 * Gets the index of the line closest to the pixel height `y`. Heights above the first linewill always
		 * return 0, and heights past the end of the last line will return the index of the last line.
		 */
		size_t get_closest_line_to_height(float y) const;

		CursorPosition get_line_start_position(size_t lineIndex) const;
		CursorPosition get_line_end_position(size_t lineIndex) const;

		CursorPosition find_closest_cursor_position(float textWidth, XAlignment, icu::BreakIterator&,
				size_t lineNumber, float cursorX) const;

		float get_line_x_start(size_t lineIndex, float textWidth, XAlignment) const;

		/**
		 * Whether the range [firstCharIndex, lastCharIndex) intersect's the run's [charStartIndex, charEndIndex)
		 */
		bool run_contains_char_range(size_t runIndex, uint32_t firstCharIndex, uint32_t lastCharIndex) const;

		/**
		 * Gets the horizontal range covered by the character range [firstCharIndex, lastCharIndex) within the
		 * given run.
		 * NOTE: Behavior is undefined if the range falls completely outside of
		 * [run.charStartIndex, run.charEndIndex)
		 */
		Pair<float, float> get_position_range_in_run(size_t runIndex, uint32_t firstCharIndex,
				uint32_t lastCharIndex) const;

		/**
		 * Gets the horizontal offset of the given cursor index from the start of its current line.
		 * NOTE: Behavior is undefined if `cursor` is outside the range of 
		 * charStartIndex < cursor <= charEndIndex
		 */
		float get_glyph_offset_in_run(size_t runIndex, uint32_t cursor) const;

		uint32_t get_first_run_index(size_t lineIndex) const;
		uint32_t get_first_glyph_index(size_t runIndex) const;
		uint32_t get_first_position_index(size_t runIndex) const;

		float get_text_start_y() const;
		float get_text_width() const;
		float get_text_height() const;

		const float* get_run_positions(size_t runIndex) const;
		uint32_t get_run_glyph_count(size_t runIndex) const;

		uint32_t get_line_run_end_index(size_t lineIndex) const;
		float get_line_width(size_t lineIndex) const;
		float get_line_height(size_t lineIndex) const;
		float get_line_ascent(size_t lineIndex) const;
		float get_line_total_descent(size_t lineIndex) const;

		const SingleScriptFont& get_run_font(size_t runIndex) const;
		uint32_t get_run_glyph_end_index(size_t runIndex) const;
		uint32_t get_run_char_start_index(size_t runIndex) const;
		uint32_t get_run_char_end_index(size_t runIndex) const;
		uint8_t get_run_char_end_offset(size_t runIndex) const;
		bool is_run_rtl(size_t runIndex) const;

		size_t get_line_count() const;
		size_t get_run_count() const;
		size_t get_glyph_count() const;
		size_t get_char_index_count() const;

		uint32_t get_glyph_id(uint32_t glyphIndex) const;
		uint32_t get_char_index(uint32_t glyphIndex) const;

		const float* get_glyph_position_data() const;
		size_t get_glyph_position_data_count() const;

		bool empty() const;

		template <typename Functor>
		void for_each_line(float textWidth, XAlignment textXAlignment, Functor&& func) const;
		template <typename Functor>
		void for_each_run(float textWidth, XAlignment textXAlignment, Functor&& func) const;
		template <typename Functor>
		void for_each_glyph(float textWidth, XAlignment textXAlignment, Functor&& func) const;
	private:
		struct VisualRun {
			SingleScriptFont font;
			uint32_t glyphEndIndex;
			uint32_t charStartIndex; // First (lowest) logical code unit index of the run
			uint32_t charEndIndex; // First logical code unit index not in the run
			uint8_t charEndOffset; // Offset ahead of charEndIndex for separator codepoints, if applicable
			bool rightToLeft;
		};

		struct LineInfo {
			uint32_t visualRunsEndIndex;
			float width;
			float ascent;
			// Total descent from the top of the paragraph to the bottom of this line. The difference between
			// this and the `totalDescent` of the previous line is the height
			float totalDescent;
		};

		std::vector<VisualRun> m_visualRuns;
		std::vector<LineInfo> m_lines;
		std::vector<uint32_t> m_glyphs;
		std::vector<uint32_t> m_charIndices;
		std::vector<float> m_glyphPositions;
		float m_textStartY{};

		float get_glyph_offset_ltr(size_t runIndex, uint32_t cursor) const;
		float get_glyph_offset_rtl(size_t runIndex, uint32_t cursor) const;
};

}

template <typename Functor>
void Text::LayoutInfo::for_each_line(float textWidth, XAlignment textXAlignment, Functor&& func) const {
	auto lineY = m_textStartY;

	for (size_t i = 0; i < m_lines.size(); ++i) {
		auto lineX = get_line_x_start(i, textWidth, textXAlignment);
		func(i, lineX, lineY + m_lines[i].ascent);
		lineY += get_line_height(i);
	}
}

template <typename Functor>
void Text::LayoutInfo::for_each_run(float textWidth, XAlignment textXAlignment, Functor&& func) const {
	uint32_t runIndex{};

	for_each_line(textWidth, textXAlignment, [&](auto lineIndex, auto lineX, auto lineY) {
		for (; runIndex < m_lines[lineIndex].visualRunsEndIndex; ++runIndex) {
			func(lineIndex, runIndex, lineX, lineY);
		}
	});
}

template <typename Functor>
void Text::LayoutInfo::for_each_glyph(float textWidth, XAlignment textXAlignment, Functor&& func) const {
	uint32_t glyphIndex{};
	uint32_t glyphPosIndex{};

	for_each_run(textWidth, textXAlignment, [&](auto, auto runIndex, auto lineX, auto lineY) {
		auto& run = m_visualRuns[runIndex];

		for (; glyphIndex < run.glyphEndIndex; ++glyphIndex, glyphPosIndex += 2) {
			func(m_glyphs[glyphIndex], m_charIndices[glyphIndex], m_glyphPositions[glyphPosIndex],
					m_glyphPositions[glyphPosIndex + 1], *run.pFont, lineX, lineY);
		}

		glyphPosIndex += 2;
	});
}

