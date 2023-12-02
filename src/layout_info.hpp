#pragma once

#include "common.hpp"
#include "cursor_position.hpp"
#include "text_alignment.hpp"
#include "value_runs.hpp"
#include "font.hpp"
#include "pair.hpp"

#include <cstdint>

#include <unicode/uversion.h>

U_NAMESPACE_BEGIN

class BreakIterator;

U_NAMESPACE_END

class MultiScriptFont;

namespace Text {

enum class LayoutInfoFlags : uint8_t {
	NONE = 0,
	RIGHT_TO_LEFT = 1, // Whether the text direction default should be RTL. Leave unset to default to LTR
	OVERRIDE_DIRECTIONALITY = 2, // Whether the configured text direction should override script directions
	VERTICAL = 4, // Whether the text is composed vertically. Leave unset for horizontal text.
};

RICHTEXT_DEFINE_ENUM_BITFLAG_OPERATORS(LayoutInfoFlags)

struct CursorPositionResult {
	float x;
	float y;
	float height;
	size_t lineNumber;
};

struct VisualRun {
	const Font* pFont;
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

struct LayoutInfo {
	std::vector<VisualRun> visualRuns;
	std::vector<LineInfo> lines;
	std::vector<uint32_t> glyphs;
	std::vector<uint32_t> charIndices;
	std::vector<float> glyphPositions;
	float textStartY;

	/**
	 * @brief Clears all layout information contained within the object.
	 */
	void clear();

	/**
	 * Calculates the pixel position, height, and line number of the text cursor given the provided
	 * `CursorPosition`.
	 *
	 * @param textWidth The width of the area containing the paragraph
	 * @param textXAlignment The X alignment of the paragraph text
	 * @param cursorPosition The position of the cursor
	 */
	CursorPositionResult calc_cursor_pixel_pos(float textWidth, TextXAlignment textXAlignment,
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

	CursorPosition find_closest_cursor_position(float textWidth, TextXAlignment, icu::BreakIterator&,
			size_t lineNumber, float cursorX) const;

	float get_line_x_start(size_t lineIndex, float textWidth, TextXAlignment) const;

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

	uint32_t get_first_run_index(size_t lineIndex) const;
	uint32_t get_first_glyph_index(size_t runIndex) const;
	uint32_t get_first_position_index(size_t runIndex) const;

	float get_line_height(size_t lineIndex) const;

	const float* get_run_positions(size_t runIndex) const;
	uint32_t get_run_glyph_count(size_t runIndex) const;

	/**
	 * Gets the horizontal offset of the given cursor index from the start of its current line.
	 * NOTE: Behavior is undefined if `cursor` is outside the range of charStartIndex < cursor <= charEndIndex
	 */
	float get_glyph_offset_in_run(size_t runIndex, uint32_t cursor) const;
	float get_glyph_offset_ltr(size_t runIndex, uint32_t cursor) const;
	float get_glyph_offset_rtl(size_t runIndex, uint32_t cursor) const;

	template <typename Functor>
	void for_each_line(float textWidth, TextXAlignment textXAlignment, Functor&& func) const;
	template <typename Functor>
	void for_each_run(float textWidth, TextXAlignment textXAlignment, Functor&& func) const;
	template <typename Functor>
	void for_each_glyph(float textWidth, TextXAlignment textXAlignment, Functor&& func) const;
};

/**
 * @brief Builds the paragraph layout using LayoutEx
 */
void build_layout_info_icu_lx(LayoutInfo& result, const char16_t* chars, int32_t count,
		const ValueRuns<const MultiScriptFont*>& fontRuns, float textAreaWidth, float textAreaHeight,
		TextYAlignment textYAlignment, LayoutInfoFlags flags);

/**
 * @brief Builds the paragraph layout using direct calls to ubidi.h and usc_impl.h run calculation functions
 */
void build_layout_info_icu(LayoutInfo& result, const char16_t* chars, int32_t count,
		const ValueRuns<const MultiScriptFont*>& fontRuns, float textAreaWidth, float textAreaHeight,
		TextYAlignment textYAlignment, LayoutInfoFlags flags);

/**
 * @brief Builds the paragraph layout using UTF-8 APIs
 */
void build_layout_info_utf8(LayoutInfo& result, const char* chars, int32_t count,
		const ValueRuns<const MultiScriptFont*>& fontRuns, float textAreaWidth, float textAreaHeight,
		TextYAlignment textYAlignment, LayoutInfoFlags flags);

/**
 * @brief Converts a UTF-16 LayoutInfo to UTF-8 based indices 
 */
void convert_layout_info_to_utf8(LayoutInfo& result, const char16_t* srcChars, int32_t srcCharCount,
		const char* dstChars, int32_t dstCharCount);

}

template <typename Functor>
void Text::LayoutInfo::for_each_line(float textWidth, TextXAlignment textXAlignment, Functor&& func) const {
	auto lineY = lines.front().ascent;

	for (size_t i = 0; i < lines.size(); ++i) {
		auto lineX = get_line_x_start(i, textWidth, textXAlignment);
		func(i, lineX, lineY);
		lineY += get_line_height(i);
	}
}

template <typename Functor>
void Text::LayoutInfo::for_each_run(float textWidth, TextXAlignment textXAlignment, Functor&& func) const {
	uint32_t runIndex{};

	for_each_line(textWidth, textXAlignment, [&](auto lineIndex, auto lineX, auto lineY) {
		for (; runIndex < lines[lineIndex].visualRunsEndIndex; ++runIndex) {
			func(lineIndex, runIndex, lineX, lineY);
		}
	});
}

template <typename Functor>
void Text::LayoutInfo::for_each_glyph(float textWidth, TextXAlignment textXAlignment, Functor&& func) const {
	uint32_t glyphIndex{};
	uint32_t glyphPosIndex{};

	for_each_run(textWidth, textXAlignment, [&](auto, auto runIndex, auto lineX, auto lineY) {
		auto& run = visualRuns[runIndex];

		for (; glyphIndex < run.glyphEndIndex; ++glyphIndex, glyphPosIndex += 2) {
			func(glyphs[glyphIndex], charIndices[glyphIndex], glyphPositions[glyphPosIndex],
					glyphPositions[glyphPosIndex + 1], *run.pFont, lineX, lineY);
		}

		glyphPosIndex += 2;
	});
}

