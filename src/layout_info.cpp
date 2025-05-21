#include "layout_info.hpp"

#include "binary_search.hpp"

#include <unicode/brkiter.h>

#include <cstring>

using namespace Text;

static bool affinity_prefer_prev_run(bool atLineBreak, bool atSoftLineBreak, bool prevRunRTL, bool nextRunRTL,
		CursorAffinity affinity);

static constexpr CursorPosition make_cursor(uint32_t position, bool oppositeAffinity) {
	return {position | (static_cast<uint32_t>(oppositeAffinity) << 31)};
}

// LayoutInfo

void LayoutInfo::clear() {
	m_visualRuns.clear();
	m_lines.clear();
	m_glyphs.clear();
	m_charIndices.clear();
	m_glyphPositions.clear();
}

void LayoutInfo::reserve_runs(size_t runCount) {
	m_visualRuns.reserve(runCount);
}

void LayoutInfo::append_glyph(uint32_t glyphID) {
	m_glyphs.emplace_back(glyphID);
}

void LayoutInfo::append_char_index(uint32_t charIndex) {
	m_charIndices.emplace_back(charIndex);
}

void LayoutInfo::append_glyph_position(float x, float y) {
	m_glyphPositions.emplace_back(x);
	m_glyphPositions.emplace_back(y);
}

void LayoutInfo::append_run(const SingleScriptFont& font, uint32_t charStartIndex, uint32_t charEndIndex,
		bool rightToLeft) {
	m_visualRuns.push_back({
		.font = font,
		.glyphEndIndex = static_cast<uint32_t>(m_glyphs.size()),
		.charStartIndex = charStartIndex,
		.charEndIndex = charEndIndex,
		.rightToLeft = rightToLeft,
	});
}

void LayoutInfo::append_line(float height, float ascent) {
	auto lastRunIndex = static_cast<uint32_t>(m_visualRuns.size()) - 1;
	auto width = m_glyphPositions[2 * (m_visualRuns[lastRunIndex].glyphEndIndex + lastRunIndex)];

	m_lines.push_back({
		.visualRunsEndIndex = static_cast<uint32_t>(m_visualRuns.size()),
		.width = width,
		.ascent = ascent,
		.totalDescent = m_lines.empty() ? height : m_lines.back().totalDescent + height,
	});
}

void LayoutInfo::append_empty_line(const SingleScriptFont& font, uint32_t charIndex, float height,
		float ascent) {
	// All inserted runs need at least 2 glyph position entries
	m_glyphPositions.emplace_back();
	m_glyphPositions.emplace_back();

	m_visualRuns.push_back({
		.font = font,
		.glyphEndIndex = m_visualRuns.empty() ? 0 : m_visualRuns.back().glyphEndIndex,
		.charStartIndex = charIndex,
		.charEndIndex = charIndex,
	});

	m_lines.push_back({
		.visualRunsEndIndex = static_cast<uint32_t>(m_visualRuns.size()),
		.ascent = ascent,
		.totalDescent = m_lines.empty() ? height : m_lines.back().totalDescent + height,
	});
}

void LayoutInfo::set_run_char_end_offset(size_t runIndex, uint8_t charEndOffset) {
	m_visualRuns[runIndex].charEndOffset = charEndOffset;
}

void LayoutInfo::set_text_start_y(float textStartY) {
	m_textStartY = textStartY;
}

VisualCursorInfo LayoutInfo::calc_cursor_pixel_pos(float textWidth, XAlignment textXAlignment,
		CursorPosition cursor) const {
	size_t lineIndex;
	auto runIndex = get_run_containing_cursor(cursor, lineIndex);
	auto lineX = get_line_x_start(lineIndex, textWidth, textXAlignment);
	auto glyphOffset = get_glyph_offset_in_run(runIndex, cursor.get_position());

	return {
		.x = lineX + glyphOffset,
		.y = m_textStartY + (lineIndex == 0 ? 0.f : m_lines[lineIndex - 1].totalDescent),
		.height = m_lines[lineIndex].totalDescent - (lineIndex == 0 ? 0.f : m_lines[lineIndex - 1].totalDescent),
		.lineNumber = static_cast<uint32_t>(lineIndex),
	};
}

size_t LayoutInfo::get_run_containing_cursor(CursorPosition cursor, size_t& outLineNumber) const {
	outLineNumber = 0;
	auto cursorPos = cursor.get_position();

	size_t firstGlyphIndex = 0;
	for (size_t i = 0; i < m_visualRuns.size(); ++i) {
		outLineNumber += i == m_lines[outLineNumber].visualRunsEndIndex;

		auto& run = m_visualRuns[i];
		auto lastGlyphIndex = run.glyphEndIndex;
		bool runBeforeLineBreak = i + 1 < m_visualRuns.size()
				&& i + 1 == m_lines[outLineNumber].visualRunsEndIndex;
		bool runAfterLineBreak = i == m_lines[outLineNumber].visualRunsEndIndex;

		bool runBeforeSoftBreak = runBeforeLineBreak && m_visualRuns[i].charEndOffset == 0;
		bool runAfterSoftBreak = runAfterLineBreak && i > 0 && m_visualRuns[i - 1].charEndOffset == 0;
		bool usePrevRunEnd = i > 0 && affinity_prefer_prev_run(runAfterLineBreak, runAfterSoftBreak,
				m_visualRuns[i - 1].rightToLeft, m_visualRuns[i].rightToLeft, cursor.get_affinity());
		bool useNextRunStart = i + 1 < m_visualRuns.size() && !affinity_prefer_prev_run(runBeforeLineBreak,
				runBeforeSoftBreak, m_visualRuns[i].rightToLeft, m_visualRuns[i + 1].rightToLeft,
				cursor.get_affinity());
		bool ignoreStart = cursorPos == run.charStartIndex && usePrevRunEnd;
		bool ignoreEnd = cursorPos == run.charEndIndex + run.charEndOffset && useNextRunStart;

		if (cursorPos >= run.charStartIndex && cursorPos <= run.charEndIndex + run.charEndOffset
				&& !ignoreStart && !ignoreEnd) {
			return i;
		}

		firstGlyphIndex = lastGlyphIndex;
	}

	return m_visualRuns.size() - 1;
}

size_t LayoutInfo::get_closest_line_to_height(float y) const {
	return binary_search(0, m_lines.size(), [&](auto index) {
		return m_lines[index].totalDescent < y;
	});
}

CursorPosition LayoutInfo::get_line_start_position(size_t lineIndex) const {
	if (m_lines.empty()) {
		return {};
	}

	auto lowestRun = get_first_run_index(lineIndex);
	auto lowestRunEnd = m_visualRuns[lowestRun].charEndIndex;

	for (uint32_t i = lowestRun + 1; i < m_lines[lineIndex].visualRunsEndIndex; ++i) {
		if (m_visualRuns[i].charEndIndex < lowestRunEnd) {
			lowestRun = i;
			lowestRunEnd = m_visualRuns[i].charEndIndex;
		}
	}

	return {m_visualRuns[lowestRun].rightToLeft ? m_visualRuns[lowestRun].charEndIndex
			: m_visualRuns[lowestRun].charStartIndex};
}

CursorPosition LayoutInfo::get_line_end_position(size_t lineIndex) const {
	if (m_lines.empty()) {
		return {};
	}

	auto highestRun = get_first_run_index(lineIndex);
	auto highestRunEnd = m_visualRuns[highestRun].charEndIndex;

	for (uint32_t i = highestRun + 1; i < m_lines[lineIndex].visualRunsEndIndex; ++i) {
		if (m_visualRuns[i].charEndIndex > highestRunEnd) {
			highestRun = i;
			highestRunEnd = m_visualRuns[i].charEndIndex;
		}
	}

	bool oppositeAffinity = highestRun == m_lines[lineIndex].visualRunsEndIndex - 1
			&& m_visualRuns[highestRun].charEndOffset == 0;
	return make_cursor(m_visualRuns[highestRun].rightToLeft ? m_visualRuns[highestRun].charStartIndex
			: m_visualRuns[highestRun].charEndIndex, oppositeAffinity);
}

float LayoutInfo::get_line_x_start(size_t lineNumber, float textWidth, XAlignment align) const {
	auto lineWidth = m_lines[lineNumber].width;

	switch (align) {
		case XAlignment::LEFT:
			return 0.f;
		case XAlignment::RIGHT:
			return textWidth - lineWidth;
		case XAlignment::CENTER:
			return 0.5f * (textWidth - lineWidth);
	}

	RICHTEXT_UNREACHABLE();
}

CursorPosition LayoutInfo::find_closest_cursor_position(float textWidth, XAlignment textXAlignment,
		icu::BreakIterator& iter, size_t lineNumber, float cursorX) const {
	if (m_lines.empty()) {
		return {};
	}

	cursorX -= get_line_x_start(lineNumber, textWidth, textXAlignment);

	// Find run containing char
	auto firstRunIndex = get_first_run_index(lineNumber);
	auto lastRunIndex = m_lines[lineNumber].visualRunsEndIndex;
	auto runIndex = binary_search(firstRunIndex, lastRunIndex - firstRunIndex, [&](auto index) {
		auto lastPosIndex = 2 * (m_visualRuns[index].glyphEndIndex + index);
		return m_glyphPositions[lastPosIndex] < cursorX;
	});

	if (runIndex == lastRunIndex) {
		CursorPosition result{m_visualRuns[lastRunIndex - 1].rightToLeft
				? m_visualRuns[lastRunIndex - 1].charStartIndex
				: m_visualRuns[lastRunIndex - 1].charEndIndex};
		result.set_affinity(CursorAffinity::OPPOSITE);
		return result;
	}

	// Find closest glyph in run
	auto firstGlyphIndex = get_first_glyph_index(runIndex);
	auto lastGlyphIndex = m_visualRuns[runIndex].glyphEndIndex;
	auto firstPosIndex = get_first_position_index(runIndex);
	bool rightToLeft = m_visualRuns[runIndex].rightToLeft;

	auto glyphIndex = firstGlyphIndex + binary_search(0, lastGlyphIndex - firstGlyphIndex, [&](auto index) {
		return m_glyphPositions[firstPosIndex + 2 * index] < cursorX;
	});

	// Find visual and logical bounds of the current glyph's cluster
	uint32_t clusterStartChar;
	uint32_t clusterEndChar;
	float clusterStartPos;
	float clusterEndPos;

	if (rightToLeft) {
		if (glyphIndex == firstGlyphIndex) {
			clusterStartChar = clusterEndChar = m_visualRuns[runIndex].charEndIndex;
			clusterStartPos = clusterEndPos = m_glyphPositions[firstPosIndex];
		}
		else {
			clusterStartChar = m_charIndices[glyphIndex - 1];
			clusterEndChar = glyphIndex - 1 == firstGlyphIndex ? m_visualRuns[runIndex].charEndIndex
					: m_charIndices[glyphIndex - 2];
			clusterStartPos = m_glyphPositions[firstPosIndex + 2 * (glyphIndex - firstGlyphIndex)];
			clusterEndPos = m_glyphPositions[firstPosIndex + 2 * (glyphIndex - 1 - firstGlyphIndex)];
		}
	}
	else {
		clusterStartChar = glyphIndex == firstGlyphIndex ? m_visualRuns[runIndex].charStartIndex
				: m_charIndices[glyphIndex - 1];
		clusterEndChar = glyphIndex == lastGlyphIndex ? m_visualRuns[runIndex].charEndIndex
				: m_charIndices[glyphIndex];
		clusterStartPos = glyphIndex == firstGlyphIndex ? m_glyphPositions[firstPosIndex]
				: m_glyphPositions[firstPosIndex + 2 * (glyphIndex - 1 - firstGlyphIndex)];
		clusterEndPos = m_glyphPositions[firstPosIndex + 2 * (glyphIndex - firstGlyphIndex)];
	}

	// Determine necessary affinity of the cursor
	bool firstRunInLine = runIndex == firstRunIndex;
	bool lastRunInLine = runIndex == lastRunIndex - 1;
	bool atSoftLineBreak = lastRunInLine && m_visualRuns[runIndex].charEndOffset == 0;

	bool firstGlyphAffinity = !firstRunInLine && !rightToLeft && m_visualRuns[runIndex - 1].rightToLeft;
	bool lastGlyphAffinity = atSoftLineBreak
			|| (!lastRunInLine && !rightToLeft && m_visualRuns[runIndex + 1].rightToLeft);

	if (clusterStartChar == clusterEndChar) {
		return make_cursor(clusterStartChar, firstGlyphAffinity);
	}

	auto currCharIndex = clusterStartChar;
	auto currPos = clusterStartPos;

	for (;;) {
		auto nextCharIndex = iter.following(currCharIndex);
		auto nextPos = clusterStartPos + static_cast<float>(nextCharIndex - clusterStartChar)
				/ static_cast<float>(clusterEndChar - clusterStartChar)
				* (clusterEndPos - clusterStartPos);
		
		if (rightToLeft) {
			if (cursorX > nextPos && cursorX <= currPos) {
				auto selectedChar = cursorX - nextPos < currPos - cursorX ? nextCharIndex : currCharIndex;
				bool affinity = (selectedChar == m_visualRuns[runIndex].charEndIndex && firstGlyphAffinity)
						|| (selectedChar == m_visualRuns[runIndex].charStartIndex && lastGlyphAffinity);
				return make_cursor(selectedChar, affinity);
			}
		}
		else {
			if (cursorX > currPos && cursorX <= nextPos) {
				auto selectedChar = nextPos - cursorX < cursorX - currPos ? nextCharIndex : currCharIndex;
				bool affinity = (selectedChar == m_visualRuns[runIndex].charStartIndex && firstGlyphAffinity)
						|| (selectedChar == m_visualRuns[runIndex].charEndIndex && lastGlyphAffinity);
				return make_cursor(selectedChar, affinity);
			}
		}
		
		if (nextCharIndex >= clusterEndChar) [[unlikely]] {
			return {clusterStartChar};
		}
		
		currCharIndex = nextCharIndex;
		currPos = nextPos;
	}

	RICHTEXT_UNREACHABLE();
}

bool LayoutInfo::run_contains_char_range(size_t runIndex, uint32_t firstCharIndex,
		uint32_t lastCharIndex) const {
	return m_visualRuns[runIndex].charStartIndex < lastCharIndex
			&& m_visualRuns[runIndex].charEndIndex > firstCharIndex;
}

Pair<float, float> LayoutInfo::get_position_range_in_run(size_t runIndex, uint32_t firstCharIndex,
		uint32_t lastCharIndex) const {
	auto& run = m_visualRuns[runIndex];
	auto minPos = get_glyph_offset_in_run(runIndex, std::max(std::min(firstCharIndex, run.charEndIndex),
			run.charStartIndex));
	auto maxPos = get_glyph_offset_in_run(runIndex, std::max(std::min(lastCharIndex, run.charEndIndex),
			run.charStartIndex));

	if (run.rightToLeft) {
		std::swap(minPos, maxPos);
	}

	return {minPos, maxPos};
}

float LayoutInfo::get_glyph_offset_in_run(size_t runIndex, uint32_t cursor) const {
	return m_visualRuns[runIndex].rightToLeft ? get_glyph_offset_rtl(runIndex, cursor)
			: get_glyph_offset_ltr(runIndex, cursor);
}

uint32_t LayoutInfo::get_first_run_index(size_t lineIndex) const {
	return lineIndex == 0 ? 0 : m_lines[lineIndex - 1].visualRunsEndIndex;
}

uint32_t LayoutInfo::get_first_glyph_index(size_t runIndex) const {
	return runIndex == 0 ? 0 : m_visualRuns[runIndex - 1].glyphEndIndex;
}

uint32_t LayoutInfo::get_first_position_index(size_t runIndex) const {
	return runIndex == 0 ? 0 : 2 * (m_visualRuns[runIndex - 1].glyphEndIndex + runIndex);
}

uint32_t LayoutInfo::get_line_run_end_index(size_t lineIndex) const {
	return m_lines[lineIndex].visualRunsEndIndex;
}

float LayoutInfo::get_line_width(size_t lineIndex) const {
	return m_lines[lineIndex].width;
}

float LayoutInfo::get_line_height(size_t lineIndex) const {
	return lineIndex == 0 ? m_lines.front().totalDescent
			: m_lines[lineIndex].totalDescent - m_lines[lineIndex - 1].totalDescent;
}

float LayoutInfo::get_line_ascent(size_t lineIndex) const {
	return m_lines[lineIndex].ascent;
}

float LayoutInfo::get_line_total_descent(size_t lineIndex) const {
	return m_lines[lineIndex].totalDescent;
}

const SingleScriptFont& LayoutInfo::get_run_font(size_t runIndex) const {
	return m_visualRuns[runIndex].font;
}

uint32_t LayoutInfo::get_run_glyph_end_index(size_t runIndex) const {
	return m_visualRuns[runIndex].glyphEndIndex;
}

uint32_t LayoutInfo::get_run_char_start_index(size_t runIndex) const {
	return m_visualRuns[runIndex].charStartIndex;
}

uint32_t LayoutInfo::get_run_char_end_index(size_t runIndex) const {
	return m_visualRuns[runIndex].charEndIndex;
}

uint8_t LayoutInfo::get_run_char_end_offset(size_t runIndex) const {
	return m_visualRuns[runIndex].charEndOffset;
}

bool LayoutInfo::is_run_rtl(size_t runIndex) const {
	return m_visualRuns[runIndex].rightToLeft;
}

const float* LayoutInfo::get_run_positions(size_t runIndex) const {
	return m_glyphPositions.data() + get_first_position_index(runIndex);
}

uint32_t LayoutInfo::get_run_glyph_count(size_t runIndex) const {
	return m_visualRuns[runIndex].glyphEndIndex - get_first_glyph_index(runIndex);
}

size_t LayoutInfo::get_line_count() const {
	return m_lines.size();
}

size_t LayoutInfo::get_run_count() const {
	return m_visualRuns.size();
}

size_t LayoutInfo::get_glyph_count() const {
	return m_glyphs.size();
}

size_t LayoutInfo::get_char_index_count() const {
	return m_charIndices.size();
}

float LayoutInfo::get_text_start_y() const {
	return m_textStartY;
}

float LayoutInfo::get_text_width() const {
	float width = 0.f;

	for (auto& line : m_lines) {
		if (line.width > width) {
			width = line.width;
		}
	}

	return width;
}

float LayoutInfo::get_text_height() const {
	return m_lines.empty() ? 0.f : m_lines.back().totalDescent;
}

uint32_t LayoutInfo::get_glyph_id(uint32_t glyphIndex) const {
	return m_glyphs[glyphIndex];
}

uint32_t LayoutInfo::get_char_index(uint32_t glyphIndex) const {
	return m_charIndices[glyphIndex];
}

const float* LayoutInfo::get_glyph_position_data() const {
	return m_glyphPositions.data();
}

size_t LayoutInfo::get_glyph_position_data_count() const {
	return m_glyphPositions.size();
}

bool LayoutInfo::empty() const {
	return m_lines.empty();
}

float LayoutInfo::get_glyph_offset_ltr(size_t runIndex, uint32_t cursor) const {
	auto firstGlyphIndex = get_first_glyph_index(runIndex);
	auto lastGlyphIndex = m_visualRuns[runIndex].glyphEndIndex;
	auto firstPosIndex = get_first_position_index(runIndex);

	float glyphOffset = 0.f;

	auto glyphIndex = binary_search(firstGlyphIndex, lastGlyphIndex - firstGlyphIndex, [&](auto index) {
		return m_charIndices[index] < cursor;
	});

	auto nextCharIndex = glyphIndex == lastGlyphIndex ? m_visualRuns[runIndex].charEndIndex
			: m_charIndices[glyphIndex];
	auto clusterDiff = nextCharIndex - cursor;

	glyphOffset = m_glyphPositions[firstPosIndex + 2 * (glyphIndex - firstGlyphIndex)];

	if (clusterDiff > 0 && glyphIndex > 0) {
		auto clusterCodeUnitCount = nextCharIndex - m_charIndices[glyphIndex - 1];
		auto prevGlyphOffset = m_glyphPositions[firstPosIndex + 2 * (glyphIndex - firstGlyphIndex - 1)];
		auto scaleFactor = static_cast<float>(clusterCodeUnitCount - clusterDiff)
				/ static_cast<float>(clusterCodeUnitCount);

		glyphOffset = prevGlyphOffset + (glyphOffset - prevGlyphOffset) * scaleFactor;
	}

	return glyphOffset;
}

float LayoutInfo::get_glyph_offset_rtl(size_t runIndex, uint32_t cursor) const {
	auto firstGlyphIndex = get_first_glyph_index(runIndex);
	auto lastGlyphIndex = m_visualRuns[runIndex].glyphEndIndex;
	auto firstPosIndex = get_first_position_index(runIndex);

	float glyphOffset = 0.f;

	auto glyphIndex = binary_search(firstGlyphIndex, lastGlyphIndex - firstGlyphIndex, [&](auto index) {
		return m_charIndices[index] >= cursor;
	});

	auto nextCharIndex = glyphIndex == firstGlyphIndex ? m_visualRuns[runIndex].charEndIndex
			: m_charIndices[glyphIndex - 1];
	auto clusterDiff = nextCharIndex - cursor;

	glyphOffset = m_glyphPositions[firstPosIndex + 2 * (glyphIndex - firstGlyphIndex)];

	if (clusterDiff > 0 && glyphIndex < lastGlyphIndex) {
		auto clusterCodeUnitCount = nextCharIndex - m_charIndices[glyphIndex];
		auto prevGlyphOffset = m_glyphPositions[firstPosIndex + 2 * (glyphIndex - firstGlyphIndex + 1)];
		auto scaleFactor = static_cast<float>(clusterCodeUnitCount - clusterDiff)
				/ static_cast<float>(clusterCodeUnitCount);

		glyphOffset = prevGlyphOffset + (glyphOffset - prevGlyphOffset) * scaleFactor;
	}

	return glyphOffset;
}

// Static Functions

static bool affinity_prefer_prev_run(bool atLineBreak, bool atSoftLineBreak, bool prevRunRTL, bool nextRunRTL,
		CursorAffinity affinity) {
	// Case 1: Current run is at a soft line break
	return (atSoftLineBreak && affinity == CursorAffinity::OPPOSITE)
			// Case 2: Transition from RTL-LTR
			|| (!atLineBreak && prevRunRTL && !nextRunRTL && affinity == CursorAffinity::DEFAULT)
			// Case 3: Transition from LTR-RTL
			|| (!atLineBreak && !prevRunRTL && nextRunRTL && affinity == CursorAffinity::OPPOSITE);
}

