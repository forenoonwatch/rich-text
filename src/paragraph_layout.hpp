#pragma once

#include "common.hpp"
#include "cursor_position.hpp"
#include "text_alignment.hpp"
#include "text_runs.hpp"
#include "font.hpp"

#include <cstdint>

namespace icu_73 { class BreakIterator; }

class MultiScriptFont;

enum class ParagraphLayoutFlags : uint8_t {
	NONE = 0,
	RIGHT_TO_LEFT = 1, // Whether the text direction default should be RTL. Leave unset to default to LTR
	OVERRIDE_DIRECTIONALITY = 2, // Whether the configured text direction should override script directions
	VERTICAL = 4, // Whether the text is composed vertically. Leave unset for horizontal text.
};

ZN_DEFINE_ENUM_BITFLAG_OPERATORS(ParagraphLayoutFlags)

struct CursorPositionResult {
	float x;
	float y;
	float height;
	size_t lineNumber;
};

struct VisualRun {
	const Font* pFont;
	uint32_t glyphEndIndex;
	uint32_t glyphPositionEndIndex; // FIXME: Unnecessary, go away
	bool rightToLeft;
};

struct LineInfo {
	uint32_t visualRunsEndIndex;
	uint32_t lastStringIndex;
	float width;
	float ascent;
	// Total descent from the top of the paragraph to the bottom of this line. The difference between
	// this and the `totalDescent` of the previous line is the height
	float totalDescent;
	// The difference between the `lastStringIndex` and the end of the last rendered char of the line.
	// Used to correct for line break characters
	uint32_t lastCharDiff;
};

struct ParagraphLayout {
	std::vector<VisualRun> visualRuns;
	std::vector<LineInfo> lines;
	std::vector<uint32_t> glyphs;
	std::vector<uint32_t> charIndices;
	std::vector<float> glyphPositions;
	float textStartY;
	bool rightToLeft;

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
	 * LTR-RTL Default: Prev Run End
	 * RTL-LTR Default: Next Run Start
	 *
	 * @param cursorPosition The position of the cursor
	 * @param outLineNumber The line number containing the cursor
	 */
	size_t get_run_containing_cursor(CursorPosition cursorPosition, size_t& outLineNumber) const;

	/**
	 * Gets the line containing the character index `charIndex`.
	 *
	 * @param charIndex Index of the character in the original string
	 */
	size_t get_line_containing_character(uint32_t charIndex) const;

	/**
	 * Gets the index of the line closest to the pixel height `y`. Heights above the first linewill always
	 * return 0, and heights past the end of the last line will return the index of the last line.
	 */
	size_t get_closest_line_to_height(float y) const;

	CursorPosition get_line_start_position(size_t lineIndex) const;
	CursorPosition get_line_end_position(size_t lineIndex) const;

	CursorPosition find_closest_cursor_position(float textWidth, TextXAlignment, icu_73::BreakIterator&,
			size_t lineNumber, float cursorX) const;

	float get_line_x_start(const LineInfo&, float textWidth, TextXAlignment) const;

	uint32_t get_first_run_index(const LineInfo&) const;
	uint32_t get_first_glyph_index(const VisualRun&) const;
	uint32_t get_first_position_index(const VisualRun&) const;

	float get_line_height(const LineInfo&) const;

	const float* get_run_positions(const VisualRun&) const;
	uint32_t get_run_glyph_count(const VisualRun&) const;

	bool is_empty_line(size_t lineIndex) const;

	template <typename Functor>
	void for_each_line(float textWidth, TextXAlignment textXAlignment, Functor&& func) const;
	template <typename Functor>
	void for_each_run(float textWidth, TextXAlignment textXAlignment, Functor&& func) const;
	template <typename Functor>
	void for_each_glyph(float textWidth, TextXAlignment textXAlignment, Functor&& func) const;
};

void build_paragraph_layout(ParagraphLayout& result, const char16_t* chars, int32_t count,
		const RichText::TextRuns<const MultiScriptFont*>& fontRuns, float textAreaWidth, float textAreaHeight,
		TextYAlignment textYAlignment, ParagraphLayoutFlags flags);

template <typename Functor>
void ParagraphLayout::for_each_line(float textWidth, TextXAlignment textXAlignment, Functor&& func) const {
	auto lineY = lines.front().ascent;

	for (size_t i = 0; i < lines.size(); ++i) {
		auto lineX = get_line_x_start(lines[i], textWidth, textXAlignment);
		func(i, lines[i], lineX, lineY);
		lineY += get_line_height(lines[i]);
	}
}

template <typename Functor>
void ParagraphLayout::for_each_run(float textWidth, TextXAlignment textXAlignment, Functor&& func) const {
	uint32_t runIndex{};

	for_each_line(textWidth, textXAlignment, [&](auto, auto& line, auto lineX, auto lineY) {
		for (; runIndex < line.visualRunsEndIndex; ++runIndex) {
			func(line, visualRuns[runIndex], lineX, lineY);
		}
	});
}

template <typename Functor>
void ParagraphLayout::for_each_glyph(float textWidth, TextXAlignment textXAlignment, Functor&& func) const {
	uint32_t glyphIndex{};
	uint32_t glyphPosIndex{};

	for_each_run(textWidth, textXAlignment, [&](auto&, auto& run, auto lineX, auto lineY) {
		glyphPosIndex += 2 * run.rightToLeft;

		for (; glyphIndex < run.glyphEndIndex; ++glyphIndex, glyphPosIndex += 2) {
			func(glyphs[glyphIndex], charIndices[glyphIndex], glyphPositions.data() + glyphPosIndex,
					*run.pFont, lineX, lineY);
		}

		glyphPosIndex += 2 * !run.rightToLeft;
	});
}

