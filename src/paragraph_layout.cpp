#include "paragraph_layout.hpp"

#include "multi_script_font.hpp"

#include <layout/ParagraphLayout.h>

#include <unicode/utext.h>

#include <cstring>

static constexpr const UChar32 CH_LF = 0x000A;
static constexpr const UChar32 CH_CR = 0x000D;
static constexpr const UChar32 CH_LSEP = 0x2028;
static constexpr const UChar32 CH_PSEP = 0x2029;

static void build_paragraph_layout_icu(ParagraphLayout& result, const char16_t* chars, int32_t count,
		const RichText::TextRuns<const MultiScriptFont*>& fontRuns, float textAreaWidth, float textAreaHeight,
		TextYAlignment textYAlignment, ParagraphLayoutFlags flags);
static uint32_t handle_line_icu(ParagraphLayout& result, icu::ParagraphLayout::Line& line, int32_t charOffset);

static bool find_offset_in_run_ltr(const ParagraphLayout& pl, const VisualRun& run, int32_t cursorIndex,
		float& outOffset);
static bool find_offset_in_run_rtl(const ParagraphLayout& pl, const VisualRun& run, int32_t cursorIndex,
		float& outOffset);

static float find_cluster_position_ltr(const ParagraphLayout& pl, const VisualRun& run, int32_t glyphIndex);
static float find_cluster_position_rtl(const ParagraphLayout& pl, const VisualRun& run, int32_t glyphIndex);

static bool find_position_in_run(const ParagraphLayout& pl, const VisualRun& run, float cursorX,
		int32_t& result);
static bool find_position_in_run_ltr(const ParagraphLayout& pl, const VisualRun& run, float cursorX,
		int32_t& result);
static bool find_position_in_run_rtl(const ParagraphLayout& pl, const VisualRun& run, float cursorX,
		int32_t& result);

template <typename Condition>
static constexpr size_t binary_search(size_t first, size_t count, Condition&& cond) {
	while (count > 0) {
		auto step = count / 2;
		auto i = first + step;

		if (cond(i)) {
			first = i + 1;
			count -= step + 1;
		}
		else {
			count = step;
		}
	}

	return first;
}

// Public Functions

void build_paragraph_layout(ParagraphLayout& result, const char16_t* chars, int32_t count,
		const RichText::TextRuns<const MultiScriptFont*>& fontRuns, float textAreaWidth, float textAreaHeight,
		TextYAlignment textYAlignment, ParagraphLayoutFlags flags) {
	build_paragraph_layout_icu(result, chars, count, fontRuns, textAreaWidth, textAreaHeight, textYAlignment,
			flags);
}

// ParagraphLayout

CursorPositionResult ParagraphLayout::calc_cursor_pixel_pos(float textWidth, TextXAlignment textXAlignment,
		CursorPosition cursor) const {
	size_t lineIndex{};
	auto runIndex = get_run_containing_cursor(cursor, lineIndex);
	auto firstGlyphIndex = get_first_glyph_index(visualRuns[runIndex]);
	auto lastGlyphIndex = visualRuns[runIndex].glyphEndIndex;
	auto firstPosIndex = get_first_position_index(visualRuns[runIndex]);
	auto lineX = get_line_x_start(lines[lineIndex], textWidth, textXAlignment);

	bool isEmptyLine = lines[lineIndex].visualRunsEndIndex == 0
			|| (lineIndex > 0 && lines[lineIndex - 1].visualRunsEndIndex
			== lines[lineIndex].visualRunsEndIndex);

	float glyphOffset = 0.f;

	if (!isEmptyLine) {
		auto glyphIndex = binary_search(firstGlyphIndex, lastGlyphIndex - firstGlyphIndex, [&](auto index) {
			return charIndices[index] < cursor.get_position();
		});

		auto nextCharIndex = glyphIndex == lastGlyphIndex && runIndex == lines[lineIndex].visualRunsEndIndex - 1
				? lines[lineIndex].lastStringIndex - lines[lineIndex].lastCharDiff
				: charIndices[glyphIndex];
		auto clusterDiff = nextCharIndex - cursor.get_position();

		glyphOffset = glyphPositions[firstPosIndex + 2 * (glyphIndex - firstGlyphIndex)];

		if (clusterDiff > 0 && glyphIndex > 0) {
			auto clusterCodeUnitCount = nextCharIndex - charIndices[glyphIndex - 1];
			auto prevGlyphOffset = glyphPositions[firstPosIndex + 2 * (glyphIndex - firstGlyphIndex - 1)];
			auto scaleFactor = static_cast<float>(clusterCodeUnitCount - clusterDiff)
					/ static_cast<float>(clusterCodeUnitCount);

			glyphOffset = prevGlyphOffset + (glyphOffset - prevGlyphOffset) * scaleFactor;
		}
	}

	return {
		.x = lineX + glyphOffset,
		.y = textStartY + (lineIndex == 0 ? 0.f : lines[lineIndex - 1].totalDescent),
		.height = lines[lineIndex].totalDescent - (lineIndex == 0 ? 0.f : lines[lineIndex - 1].totalDescent),
		.lineNumber = lineIndex,
	};
}

size_t ParagraphLayout::get_run_containing_cursor(CursorPosition cursor, size_t& outLineNumber) const {
	outLineNumber = get_line_containing_character(cursor.get_position());

	if (outLineNumber == lines.size()) {
		--outLineNumber;
		return visualRuns.size() - 1;
	}

	auto firstRunIndex = get_first_run_index(lines[outLineNumber]);
	auto lastRunIndex = lines[outLineNumber].visualRunsEndIndex;
	// Last `lastStringIndex` is always strlen
	auto stringEnd = lines.back().lastStringIndex;

	auto runIndex = binary_search(firstRunIndex, lastRunIndex - firstRunIndex, [&](auto index) {
		auto glyphIndex = visualRuns[index].glyphEndIndex;
		auto charIndex = glyphIndex == charIndices.size() ? stringEnd : charIndices[glyphIndex];
		return charIndex <= cursor.get_position();
	});

	if (runIndex == 0) {
		return runIndex;
	}
	else if (runIndex == lastRunIndex) {
		return runIndex - 1;
	}

	auto& run = visualRuns[runIndex];
	auto& prevRun = visualRuns[runIndex - 1];

	// Cursor is on the boundary of 2 runs
	if (cursor.get_position() == charIndices[prevRun.glyphEndIndex]) {
		auto lineFirstRunIndex = outLineNumber == 0 ? 0 : lines[outLineNumber - 1].visualRunsEndIndex;
		//printf("%u is line first run %s\n", cursor.get_position(), lineFirstRunIndex == runIndex ? "y" : "n");

		// Case 1: Current run is the first run of a line, meaning we are at a line break
		/*if (lineFirstRunIndex == runIndex) {
			if (cursor.get_affinity() == CursorAffinity::OPPOSITE) {
				--outLineNumber;
				--runIndex;
			}
		}
		// Case 2: Transition from RTL-LTR
		else if (prevRun.rightToLeft && !run.rightToLeft) {
			if (cursor.get_affinity() == CursorAffinity::DEFAULT) {
				--outLineNumber;
				--runIndex;
			}
		}
		// Case 3: Transition from LTR-RTL
		else if (!prevRun.rightToLeft && run.rightToLeft) {
			if (cursor.get_affinity() == CursorAffinity::OPPOSITE) {
				--outLineNumber;
				--runIndex;
			}
		}*/
	}

	return runIndex;
}

size_t ParagraphLayout::get_line_containing_character(uint32_t charIndex) const {
	return binary_search(0, lines.size(), [&](auto index) {
		return lines[index].lastStringIndex <= charIndex;
	});
}

size_t ParagraphLayout::get_closest_line_to_height(float y) const {
	return binary_search(0, lines.size(), [&](auto index) {
		return lines[index].totalDescent < y;
	});
}

float ParagraphLayout::get_line_x_start(const LineInfo& line, float textWidth, TextXAlignment align) const {
	auto lineWidth = line.width;

	switch (align) {
		case TextXAlignment::LEFT:
			return rightToLeft == UBIDI_RTL ? textWidth - lineWidth : 0.f;
		case TextXAlignment::RIGHT:
			return textWidth - lineWidth;
		case TextXAlignment::CENTER:
			return 0.5f * (textWidth - lineWidth);
	}

	// FIXME: Assert unreachable
	return 0.f;
}

int32_t ParagraphLayout::get_line_char_start_index(const LineInfo& line) const {
	auto& firstRun = visualRuns[get_first_run_index(line)];
	auto firstGlyphIndex = get_first_glyph_index(firstRun);
	return std::min(charIndices[firstGlyphIndex], charIndices[firstRun.glyphEndIndex - 1]);
}

int32_t ParagraphLayout::get_line_char_end_index(const LineInfo& line) const {
	auto& lastRun = visualRuns[line.visualRunsEndIndex == 0 ? 0 : line.visualRunsEndIndex - 1];
	auto firstGlyphIndex = get_first_glyph_index(lastRun);
	return std::max(charIndices[firstGlyphIndex], charIndices[lastRun.glyphEndIndex - 1]);
}

float ParagraphLayout::get_cursor_offset_in_line(const LineInfo& line, int32_t cursorIndex) const {
	auto firstRunIndex = &line == lines.data() ? 0 : (&line)[-1].visualRunsEndIndex;

	if (line.visualRunsEndIndex > firstRunIndex) {
		return 0.f;
	}

	for (uint32_t i = firstRunIndex; i < line.visualRunsEndIndex; ++i) {
		auto& run = visualRuns[i];
		float offset;
		// FIXME: I don't need to iterate the entire run to know if the cursor is in it
		bool found = run.rightToLeft ? find_offset_in_run_rtl(*this, run, cursorIndex, offset)
				: find_offset_in_run_ltr(*this, run, cursorIndex, offset);

		if (found) {
			return offset;
		}
	}

	// FIXME: Assert unreachable?
	return 0.f;
}

float ParagraphLayout::get_line_end_position(const LineInfo& line) const {
	auto firstRunIndex = get_first_run_index(line);

	if (line.visualRunsEndIndex > firstRunIndex) {
		auto& run = visualRuns[line.visualRunsEndIndex - 1];
		auto firstPosIndex = get_first_position_index(run);
		return run.rightToLeft ? glyphPositions[firstPosIndex] : glyphPositions[run.glyphPositionEndIndex - 2];
	}
	else {
		return 0.f;
	}
}

int32_t ParagraphLayout::find_line_start_containing_index(int32_t index) const {
	// FIXME: I don't need to iterate all lines to know if the cursor is in it
	for (size_t lineNumber = 0; lineNumber < lines.size(); ++lineNumber) {
		auto& line = lines[lineNumber];

		auto lineStart = get_line_char_start_index(line);
		auto lineEnd = get_line_char_end_index(line);

		if (index >= lineStart && lineNumber < lines.size() - 1) {
			auto& nextLine = lines[lineNumber + 1];
			auto nextStart = get_line_char_start_index(nextLine);

			if (index < nextStart) {
				return lineStart;
			}
		}
		else if (index >= lineStart) {
			return lineStart;
		}
	}

	return {};
}

int32_t ParagraphLayout::find_line_end_containing_index(int32_t index, int32_t textEnd,
		icu::BreakIterator& iter) const {
	// FIXME: I don't need to iterate all lines to know if the cursor is in it
	for (size_t lineNumber = 0; lineNumber < lines.size(); ++lineNumber) {
		auto& line = lines[lineNumber];

		auto lineStart = get_line_char_start_index(line);
		auto lineEnd = get_line_char_end_index(line);

		if (index >= lineStart && lineNumber < lines.size() - 1) {
			auto& nextLine = lines[lineNumber + 1];
			auto nextStart = get_line_char_start_index(nextLine);

			if (index < nextStart) {
				return iter.following(lineEnd);
			}
		}
		else if (index >= lineStart) {
			return textEnd;
		}
	}

	return {};
}

int32_t ParagraphLayout::find_closest_cursor_position(float textWidth, TextXAlignment textXAlignment,
		int32_t textLength, icu::BreakIterator& iter, size_t lineNumber, float cursorX) const {
	auto& line = lines[lineNumber];
	auto lineX = get_line_x_start(line, textWidth, textXAlignment);
	auto lineEndPos = lineX + get_rightmost_line_position(line);
	int32_t result = 0;
	auto firstRunIndex = get_first_run_index(line);

	if (cursorX <= lineX) {
		return get_leftmost_char_index(line, iter);
	}
	else if (cursorX >= lineEndPos) {
		return get_rightmost_char_index(line, iter);
	}

	for (uint32_t runID = firstRunIndex; runID < line.visualRunsEndIndex; ++runID) {
		auto& run = visualRuns[runID];

		if (find_position_in_run(*this, run, cursorX - lineX, result)) {
			if (result == run.glyphEndIndex) {
				if (runID < line.visualRunsEndIndex - 1) {
					return charIndices[result];
				}
				else {
					return run.rightToLeft ? get_line_char_start_index(line)
							: iter.following(get_line_char_end_index(line));
				}
			}
			else {
				return charIndices[result];
			}
		}
	}

	return result;
}

int32_t ParagraphLayout::get_leftmost_char_index(const LineInfo& line, icu::BreakIterator& iter) const {
	auto& firstRun = visualRuns[get_first_run_index(line)];
	auto firstGlyphIndex = get_first_glyph_index(firstRun);
	return firstRun.rightToLeft ? iter.following(firstGlyphIndex) : firstGlyphIndex;
}

int32_t ParagraphLayout::get_rightmost_char_index(const LineInfo& line, icu::BreakIterator& iter) const {
	auto& lastRun = line.visualRunsEndIndex == 0 ? visualRuns.front() : visualRuns[line.visualRunsEndIndex - 1];
	auto lastGlyphIndex = lastRun.glyphEndIndex == 0 ? 0 : lastRun.glyphEndIndex - 1;
	return lastRun.rightToLeft ? lastGlyphIndex : iter.following(lastGlyphIndex);
}

float ParagraphLayout::get_rightmost_line_position(const LineInfo& line) const {
	auto& lastRun = line.visualRunsEndIndex == 0 ? visualRuns.front() : visualRuns[line.visualRunsEndIndex - 1];
	auto firstPosIndex = get_first_position_index(lastRun);
	auto lastPosIndex = lastRun.glyphPositionEndIndex == 0 ? 0 : lastRun.glyphPositionEndIndex - 2;
	return std::max(glyphPositions[firstPosIndex], glyphPositions[lastPosIndex]);
}

uint32_t ParagraphLayout::get_first_run_index(const LineInfo& line) const {
	return &line == lines.data() ? 0 : (&line)[-1].visualRunsEndIndex;
}

uint32_t ParagraphLayout::get_first_glyph_index(const VisualRun& run) const {
	return &run == visualRuns.data() ? 0 : (&run)[-1].glyphEndIndex;
}

uint32_t ParagraphLayout::get_first_position_index(const VisualRun& run) const {
	return &run == visualRuns.data() ? 0 : (&run)[-1].glyphPositionEndIndex;
}

float ParagraphLayout::get_line_height(const LineInfo& line) const {
	return &line == lines.data() ? lines.front().totalDescent : line.totalDescent - (&line)[-1].totalDescent;
}

const float* ParagraphLayout::get_run_positions(const VisualRun& run) const {
	return glyphPositions.data() + get_first_position_index(run);
}

uint32_t ParagraphLayout::get_run_glyph_count(const VisualRun& run) const {
	return run.glyphEndIndex - get_first_glyph_index(run);
}

// Static Functions

static void build_paragraph_layout_icu(ParagraphLayout& result, const char16_t* chars, int32_t count,
		const RichText::TextRuns<const MultiScriptFont*>& fontRuns, float textAreaWidth, float textAreaHeight,
		TextYAlignment textYAlignment, ParagraphLayoutFlags flags) {
	RichText::TextRuns<const MultiScriptFont*> subsetFontRuns(fontRuns.get_value_count());
	std::vector<uint32_t> lineFirstCharIndices;

	auto* start = chars;
	auto* end = start + count;
	UText iter UTEXT_INITIALIZER;
	UErrorCode err{};
	utext_openUChars(&iter, chars, count, &err);

	int32_t byteIndex = 0;

	UBiDiLevel paragraphLevel = ((flags & ParagraphLayoutFlags::RIGHT_TO_LEFT) == ParagraphLayoutFlags::NONE)
			? UBIDI_DEFAULT_LTR : UBIDI_DEFAULT_RTL;

	if ((flags & ParagraphLayoutFlags::OVERRIDE_DIRECTIONALITY) != ParagraphLayoutFlags::NONE) {
		paragraphLevel |= UBIDI_LEVEL_OVERRIDE;
	}

	for (;;) {
		auto idx = UTEXT_GETNATIVEINDEX(&iter);
		auto c = UTEXT_NEXT32(&iter);

		if (c == U_SENTINEL || c == CH_LF || c == CH_CR || c == CH_LSEP || c == CH_PSEP) {
			if (idx != byteIndex) {
				auto byteCount = idx - byteIndex;

				subsetFontRuns.clear();
				fontRuns.get_runs_subset(byteIndex, byteCount, subsetFontRuns);

				LEErrorCode err{};
				auto** ppFonts = const_cast<const MultiScriptFont**>(subsetFontRuns.get_values());
				icu::FontRuns fontRuns(reinterpret_cast<const icu::LEFontInstance**>(ppFonts),
						subsetFontRuns.get_limits(), subsetFontRuns.get_value_count());
				icu::ParagraphLayout pl(chars + byteIndex, byteCount, &fontRuns, nullptr,
						nullptr, nullptr, paragraphLevel, false, err);

				if (paragraphLevel == UBIDI_DEFAULT_LTR) {
					paragraphLevel = pl.getParagraphLevel();
				}

				while (auto* pLine = pl.nextLine(textAreaWidth)) {
					auto firstCharIndex = handle_line_icu(result, *pLine, byteIndex);
					lineFirstCharIndices.emplace_back(firstCharIndex);
					delete pLine;
				}
			}
			else {
				auto* pFont = fontRuns.get_value(byteIndex == count ? count - 1 : byteIndex);
				auto height = static_cast<float>(pFont->getAscent() + pFont->getDescent());

				lineFirstCharIndices.emplace_back(byteIndex);

				result.lines.push_back({
					.visualRunsEndIndex = static_cast<uint32_t>(result.visualRuns.size()),
					.width = 0.f,
					.ascent = static_cast<float>(pFont->getAscent()),
					.totalDescent = result.lines.empty() ? height : result.lines.back().totalDescent + height,
				});
			}

			if (c == U_SENTINEL) {
				break;
			}
			else if (c == CH_CR && UTEXT_CURRENT32(&iter) == CH_LF) {
				UTEXT_NEXT32(&iter);
			}

			byteIndex = UTEXT_GETNATIVEINDEX(&iter);

			if (!result.lines.empty()) {
				result.lines.back().lastCharDiff = byteIndex - idx;
			}
		}
	}

	// Populate lastStringIndex
	for (size_t i = 0; i < result.lines.size() - 1; ++i) {
		result.lines[i].lastStringIndex = lineFirstCharIndices[i + 1];
	}

	result.lines.back().lastStringIndex = static_cast<uint32_t>(count);

	auto totalHeight = result.lines.empty() ? 0.f : result.lines.back().totalDescent;
	result.textStartY = static_cast<float>(textYAlignment) * (textAreaHeight - totalHeight) * 0.5f;
}

static uint32_t handle_line_icu(ParagraphLayout& result, icu::ParagraphLayout::Line& line, int32_t charOffset) {
	result.visualRuns.reserve(result.visualRuns.size() + line.countRuns());

	auto* pFirstRun = line.getVisualRun(0);
	auto firstCharIndex = charOffset + (pFirstRun->getDirection() == UBIDI_LTR
			? pFirstRun->getGlyphToCharMap()[0]
			: pFirstRun->getGlyphToCharMap()[pFirstRun->getGlyphCount() - 1]);

	int32_t maxAscent = 0;
	int32_t maxDescent = 0;

	for (int32_t i = 0; i < line.countRuns(); ++i) {
		auto* pRun = line.getVisualRun(i);
		auto ascent = pRun->getAscent();
		auto descent = pRun->getDescent();
		auto* pGlyphs = pRun->getGlyphs();
		auto* pGlyphPositions = pRun->getPositions();
		auto* pCharMap = pRun->getGlyphToCharMap();

		if (ascent > maxAscent) {
			maxAscent = ascent;
		}

		if (descent > maxDescent) {
			maxDescent = descent;
		}

		if (pRun->getDirection() != UBIDI_LTR) {
			result.glyphPositions.emplace_back(pGlyphPositions[2 * pRun->getGlyphCount()]);
			result.glyphPositions.emplace_back(pGlyphPositions[2 * pRun->getGlyphCount() + 1]);
		}

		for (int32_t j = 0; j < pRun->getGlyphCount(); ++j) {
			auto glyphIndex = pRun->getDirection() == UBIDI_LTR ? j : pRun->getGlyphCount() - 1 - j;

			if (pGlyphs[glyphIndex] != 0xFFFF) {
				result.glyphs.emplace_back(pGlyphs[glyphIndex]);
				result.charIndices.emplace_back(pCharMap[glyphIndex] + charOffset);
				result.glyphPositions.emplace_back(pGlyphPositions[2 * glyphIndex]);
				result.glyphPositions.emplace_back(pGlyphPositions[2 * glyphIndex + 1]);
			}
		}

		if (pRun->getDirection() == UBIDI_LTR) {
			result.glyphPositions.emplace_back(pGlyphPositions[2 * pRun->getGlyphCount()]);
			result.glyphPositions.emplace_back(pGlyphPositions[2 * pRun->getGlyphCount() + 1]);
		}

		result.visualRuns.push_back({
			.pFont = static_cast<const Font*>(pRun->getFont()),
			.glyphEndIndex = static_cast<uint32_t>(result.glyphs.size()),
			.glyphPositionEndIndex = static_cast<uint32_t>(result.glyphPositions.size()),
			.rightToLeft = pRun->getDirection() == UBIDI_RTL,
		});
	}

	auto height = static_cast<float>(maxAscent + maxDescent);

	result.lines.push_back({
		.visualRunsEndIndex = static_cast<uint32_t>(result.visualRuns.size()),
		.width = static_cast<float>(line.getWidth()),
		.ascent = static_cast<float>(maxAscent),
		.totalDescent = result.lines.empty() ? height : result.lines.back().totalDescent + height,
	});

	return static_cast<uint32_t>(firstCharIndex);
}

static bool find_offset_in_run_ltr(const ParagraphLayout& pl, const VisualRun& run, int32_t cursorIndex,
		float& outOffset) {
	auto firstGlyphIndex = pl.get_first_glyph_index(run);
	auto posIndex = pl.get_first_position_index(run);

	for (uint32_t i = firstGlyphIndex; i < run.glyphEndIndex; ++i, posIndex += 2) {
		if (pl.charIndices[i] == cursorIndex) {
			if (pl.glyphs[i] == 0xFFFF) {
				outOffset = find_cluster_position_ltr(pl, run, i);
			}
			else {
				outOffset = pl.glyphPositions[posIndex];
			}

			return true;
		}
	}

	return false;
}

static bool find_offset_in_run_rtl(const ParagraphLayout& pl, const VisualRun& run, int32_t cursorIndex,
		float& outOffset) {
	auto firstGlyphIndex = pl.get_first_glyph_index(run);
	auto posIndex = pl.get_first_position_index(run);

	for (uint32_t i = firstGlyphIndex; i < run.glyphEndIndex; ++i, posIndex += 2) {
		if (pl.charIndices[i] == cursorIndex) {
			if (pl.glyphs[i] == 0xFFFF || (i > 0 && pl.glyphs[i - 1] == 0xFFFF)) {
				outOffset = find_cluster_position_rtl(pl, run, i);
			}
			else {
				outOffset = pl.glyphPositions[posIndex];
			}

			return true;
		}
	}

	return false;
}

static float find_cluster_position_ltr(const ParagraphLayout& pl, const VisualRun& run, int32_t glyphIndex) {
	auto firstGlyphIndex = pl.get_first_glyph_index(run);
	auto* posData = pl.get_run_positions(run);

	auto clusterStart = glyphIndex;
	auto clusterEnd = glyphIndex;

	while (clusterStart > 0 && pl.glyphs[clusterStart] == 0xFFFF) {
		--clusterStart;
	}

	while (clusterEnd < run.glyphEndIndex && pl.glyphs[clusterEnd] == 0xFFFF) {
		++clusterEnd;
	}

	auto startPos = posData[2 * (clusterStart - firstGlyphIndex)];
	auto endPos = posData[2 * (clusterEnd - firstGlyphIndex)];

	return startPos + (endPos - startPos) * static_cast<float>(glyphIndex - clusterStart)
			/ static_cast<float>(clusterEnd - clusterStart);
}

static float find_cluster_position_rtl(const ParagraphLayout& pl, const VisualRun& run, int32_t glyphIndex) {
	auto firstGlyphIndex = pl.get_first_glyph_index(run);
	auto* posData = pl.get_run_positions(run);

	auto clusterStart = glyphIndex - (glyphIndex != firstGlyphIndex);
	auto clusterEnd = glyphIndex;

	while (clusterStart > 0 && pl.glyphs[clusterStart] == 0xFFFF) {
		--clusterStart;
	}

	while (clusterEnd < run.glyphEndIndex && pl.glyphs[clusterEnd] == 0xFFFF) {
		++clusterEnd;
	}

	++clusterEnd;

	auto startPos = posData[2 * (clusterStart - firstGlyphIndex)];
	auto endPos = posData[2 * (clusterEnd - firstGlyphIndex)];

	return startPos + (endPos - startPos) * static_cast<float>(glyphIndex + 1 - clusterStart)
			/ static_cast<float>(clusterEnd - clusterStart);
}

static bool find_position_in_run(const ParagraphLayout& pl, const ::VisualRun& run, float cursorX,
		int32_t& result) {
	return run.rightToLeft ? find_position_in_run_rtl(pl, run, cursorX, result)
			: find_position_in_run_ltr(pl, run, cursorX, result);
}

static bool find_position_in_run_ltr(const ParagraphLayout& pl, const VisualRun& run, float cursorX,
		int32_t& result) {
	auto firstGlyphIndex = pl.get_first_glyph_index(run);
	auto* posData = pl.get_run_positions(run);

	for (uint32_t i = firstGlyphIndex; i < run.glyphEndIndex; ++i) {
		// Cluster
		if (i < run.glyphEndIndex - 1 && pl.glyphs[i + 1] == 0xFFFF) {
			auto clusterEnd = i + 1;
			while (clusterEnd < run.glyphEndIndex && pl.glyphs[clusterEnd] == 0xFFFF) {
				++clusterEnd;
			}

			auto posX = posData[2 * (i - firstGlyphIndex)];
			auto nextPosX = posData[2 * (clusterEnd - firstGlyphIndex)];

			if (cursorX >= posX && cursorX <= nextPosX) {
				result = i + static_cast<int32_t>((clusterEnd - i) * (cursorX - posX) / (nextPosX - posX)
						+ 0.5f);
				return true;
			}

			i = clusterEnd - 1;
		}
		else {
			auto posX = posData[2 * (i - firstGlyphIndex)];
			auto nextPosX = posData[2 * (i - firstGlyphIndex) + 2];

			if (cursorX >= posX && cursorX <= nextPosX) {
				result = cursorX - posX < nextPosX - cursorX ? i : i + 1;
				return true;
			}
		}
	}

	return false;
}

static bool find_position_in_run_rtl(const ParagraphLayout& pl, const VisualRun& run, float cursorX,
		int32_t& result) {
	auto firstGlyphIndex = pl.get_first_glyph_index(run);
	auto* posData = pl.get_run_positions(run);

	for (uint32_t i = run.glyphEndIndex; i > firstGlyphIndex; --i) {
		// Cluster
		if (i > 2 && pl.glyphs[i - 2] == 0xFFFF) {
			auto clusterStart = i - 1;
			auto clusterEnd = i - 2;
			while (clusterEnd > 0 && pl.glyphs[clusterEnd] == 0xFFFF) {
				--clusterEnd;
			}

			auto posX = posData[2 * (clusterEnd - firstGlyphIndex)];
			auto nextPosX = posData[2 * (clusterStart - firstGlyphIndex) + 2];

			if (cursorX >= posX && cursorX <= nextPosX) {
				auto clusterSize = clusterStart - clusterEnd + 1;
				result = clusterEnd + static_cast<int32_t>(clusterSize * (cursorX - posX) / (nextPosX - posX)
						+ 0.5f) - 1;
				return true;
			}

			i = clusterEnd;
		}
		else {
			auto posX = posData[2 * (i - firstGlyphIndex) - 2];
			auto nextPosX = posData[2 * (i - firstGlyphIndex)];

			if (cursorX >= posX && cursorX <= nextPosX) {
				result = cursorX - posX <= nextPosX - cursorX ? i - 2 : i - 1;
				return true;
			}
		}
	}

	return false;
}

