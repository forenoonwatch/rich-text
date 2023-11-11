#include "paragraph_layout.hpp"

#include "binary_search.hpp"
#include "multi_script_font.hpp"

#include <layout/ParagraphLayout.h>

#include <unicode/utext.h>
#include <unicode/utf16.h>

#include <cstring>

static constexpr const UChar32 CH_LF = 0x000A;
static constexpr const UChar32 CH_CR = 0x000D;
static constexpr const UChar32 CH_LSEP = 0x2028;
static constexpr const UChar32 CH_PSEP = 0x2029;

static void handle_line_icu_lx(ParagraphLayout& result, icu::ParagraphLayout::Line& line, int32_t charOffset,
		const char16_t* chars, size_t& highestRun, int32_t& highestRunCharEnd);

static bool affinity_prefer_prev_run(bool atLineBreak, bool atSoftLineBreak, bool prevRunRTL, bool nextRunRTL,
		CursorAffinity affinity);

static constexpr CursorPosition make_cursor(uint32_t position, bool oppositeAffinity) {
	return {position | (static_cast<uint32_t>(oppositeAffinity) << 31)};
}

// ParagraphLayout

CursorPositionResult ParagraphLayout::calc_cursor_pixel_pos(float textWidth, TextXAlignment textXAlignment,
		CursorPosition cursor) const {
	size_t lineIndex;
	auto runIndex = get_run_containing_cursor(cursor, lineIndex);
	auto lineX = get_line_x_start(lineIndex, textWidth, textXAlignment);

	auto glyphOffset = visualRuns[runIndex].rightToLeft
			? get_glyph_offset_rtl(runIndex, cursor.get_position())
			: get_glyph_offset_ltr(runIndex, cursor.get_position());

	return {
		.x = lineX + glyphOffset,
		.y = textStartY + (lineIndex == 0 ? 0.f : lines[lineIndex - 1].totalDescent),
		.height = lines[lineIndex].totalDescent - (lineIndex == 0 ? 0.f : lines[lineIndex - 1].totalDescent),
		.lineNumber = lineIndex,
	};
}

size_t ParagraphLayout::get_run_containing_cursor(CursorPosition cursor, size_t& outLineNumber) const {
	outLineNumber = 0;
	auto cursorPos = cursor.get_position();

	size_t firstGlyphIndex = 0;
	for (size_t i = 0; i < visualRuns.size(); ++i) {
		auto& run = visualRuns[i];
		auto lastGlyphIndex = run.glyphEndIndex;
		bool runBeforeLineBreak = i + 1 < visualRuns.size() && i + 1 == lines[outLineNumber].visualRunsEndIndex;
		bool runAfterLineBreak = i == lines[outLineNumber].visualRunsEndIndex;

		if (runAfterLineBreak) {
			++outLineNumber;
		}

		bool runBeforeSoftBreak = runBeforeLineBreak && visualRuns[i].charEndOffset == 0;
		bool runAfterSoftBreak = runAfterLineBreak && i > 0 && visualRuns[i - 1].charEndOffset == 0;
		bool usePrevRunEnd = i > 0 && affinity_prefer_prev_run(runAfterLineBreak, runAfterSoftBreak,
				visualRuns[i - 1].rightToLeft, visualRuns[i].rightToLeft, cursor.get_affinity());
		bool useNextRunStart = i + 1 < visualRuns.size() && !affinity_prefer_prev_run(runBeforeLineBreak,
				runBeforeSoftBreak, visualRuns[i].rightToLeft, visualRuns[i + 1].rightToLeft,
				cursor.get_affinity());
		bool ignoreStart = cursorPos == run.charStartIndex && usePrevRunEnd;
		bool ignoreEnd = cursorPos == run.charEndIndex + run.charEndOffset && useNextRunStart;

		if (cursorPos >= run.charStartIndex && cursorPos <= run.charEndIndex + run.charEndOffset
				&& !ignoreStart && !ignoreEnd) {
			return i;
		}

		firstGlyphIndex = lastGlyphIndex;
	}

	return visualRuns.size() - 1;
}

size_t ParagraphLayout::get_closest_line_to_height(float y) const {
	return binary_search(0, lines.size(), [&](auto index) {
		return lines[index].totalDescent < y;
	});
}

CursorPosition ParagraphLayout::get_line_start_position(size_t lineIndex) const {
	auto lowestRun = get_first_run_index(lineIndex);
	auto lowestRunEnd = visualRuns[lowestRun].charEndIndex;

	for (uint32_t i = lowestRun + 1; i < lines[lineIndex].visualRunsEndIndex; ++i) {
		if (visualRuns[i].charEndIndex < lowestRunEnd) {
			lowestRun = i;
			lowestRunEnd = visualRuns[i].charEndIndex;
		}
	}

	return {visualRuns[lowestRun].rightToLeft ? visualRuns[lowestRun].charEndIndex
			: visualRuns[lowestRun].charStartIndex};
}

CursorPosition ParagraphLayout::get_line_end_position(size_t lineIndex) const {
	auto highestRun = get_first_run_index(lineIndex);
	auto highestRunEnd = visualRuns[highestRun].charEndIndex;

	for (uint32_t i = highestRun + 1; i < lines[lineIndex].visualRunsEndIndex; ++i) {
		if (visualRuns[i].charEndIndex > highestRunEnd) {
			highestRun = i;
			highestRunEnd = visualRuns[i].charEndIndex;
		}
	}

	bool oppositeAffinity = highestRun == lines[lineIndex].visualRunsEndIndex - 1
			&& visualRuns[highestRun].charEndOffset == 0;
	return make_cursor(visualRuns[highestRun].rightToLeft ? visualRuns[highestRun].charStartIndex
			: visualRuns[highestRun].charEndIndex, oppositeAffinity);
}

float ParagraphLayout::get_line_x_start(size_t lineNumber, float textWidth, TextXAlignment align) const {
	auto lineWidth = lines[lineNumber].width;

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

CursorPosition ParagraphLayout::find_closest_cursor_position(float textWidth, TextXAlignment textXAlignment,
		icu::BreakIterator& iter, size_t lineNumber, float cursorX) const {
	cursorX -= get_line_x_start(lineNumber, textWidth, textXAlignment);

	// Find run containing char
	auto firstRunIndex = get_first_run_index(lineNumber);
	auto lastRunIndex = lines[lineNumber].visualRunsEndIndex;
	auto runIndex = binary_search(firstRunIndex, lastRunIndex - firstRunIndex, [&](auto index) {
		auto lastPosIndex = 2 * (visualRuns[index].glyphEndIndex + index);
		return glyphPositions[lastPosIndex] < cursorX;
	});

	if (runIndex == lastRunIndex) {
		return {visualRuns.back().rightToLeft ? visualRuns.back().charStartIndex
				: visualRuns.back().charEndIndex + visualRuns.back().charEndOffset};
	}

	// Find closest glyph in run
	auto firstGlyphIndex = get_first_glyph_index(runIndex);
	auto lastGlyphIndex = visualRuns[runIndex].glyphEndIndex;
	auto firstPosIndex = get_first_position_index(runIndex);
	bool rightToLeft = visualRuns[runIndex].rightToLeft;

	auto glyphIndex = firstGlyphIndex + binary_search(0, lastGlyphIndex - firstGlyphIndex, [&](auto index) {
		return glyphPositions[firstPosIndex + 2 * index] < cursorX;
	});

	// Find visual and logical bounds of the current glyph's cluster
	uint32_t clusterStartChar;
	uint32_t clusterEndChar;
	float clusterStartPos;
	float clusterEndPos;

	if (rightToLeft) {
		if (glyphIndex == firstGlyphIndex) {
			clusterStartChar = clusterEndChar = visualRuns[runIndex].charEndIndex;
			clusterStartPos = clusterEndPos = glyphPositions[firstPosIndex];
		}
		else {
			clusterStartChar = charIndices[glyphIndex - 1];
			clusterEndChar = glyphIndex - 1 == firstGlyphIndex ? visualRuns[runIndex].charEndIndex
					: charIndices[glyphIndex - 2];
			clusterStartPos = glyphPositions[firstPosIndex + 2 * (glyphIndex - firstGlyphIndex)];
			clusterEndPos = glyphPositions[firstPosIndex + 2 * (glyphIndex - 1 - firstGlyphIndex)];
		}
	}
	else {
		clusterStartChar = glyphIndex == firstGlyphIndex ? visualRuns[runIndex].charStartIndex
				: charIndices[glyphIndex - 1];
		clusterEndChar = glyphIndex == lastGlyphIndex ? visualRuns[runIndex].charEndIndex
				: charIndices[glyphIndex];
		clusterStartPos = glyphIndex == firstGlyphIndex ? glyphPositions[firstPosIndex]
				: glyphPositions[firstPosIndex + 2 * (glyphIndex - 1 - firstGlyphIndex)];
		clusterEndPos = glyphPositions[firstPosIndex + 2 * (glyphIndex - firstGlyphIndex)];
	}

	// Determine necessary affinity of the cursor
	bool firstRunInLine = runIndex == firstRunIndex;
	bool lastRunInLine = runIndex == lastRunIndex - 1;
	bool atSoftLineBreak = lastRunInLine && visualRuns[runIndex].charEndOffset == 0;

	bool firstGlyphAffinity = !firstRunInLine && !rightToLeft && visualRuns[runIndex - 1].rightToLeft;
	bool lastGlyphAffinity = atSoftLineBreak
			|| (!lastRunInLine && !rightToLeft && visualRuns[runIndex + 1].rightToLeft);

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
				bool affinity = (selectedChar == visualRuns[runIndex].charEndIndex && firstGlyphAffinity)
						|| (selectedChar == visualRuns[runIndex].charStartIndex && lastGlyphAffinity);
				return make_cursor(selectedChar, affinity);
			}
		}
		else {
			if (cursorX > currPos && cursorX <= nextPos) {
				auto selectedChar = nextPos - cursorX < cursorX - currPos ? nextCharIndex : currCharIndex;
				bool affinity = (selectedChar == visualRuns[runIndex].charStartIndex && firstGlyphAffinity)
						|| (selectedChar == visualRuns[runIndex].charEndIndex && lastGlyphAffinity);
				return make_cursor(selectedChar, affinity);
			}
		}
		
		if (nextCharIndex >= clusterEndChar) [[unlikely]] {
			return {clusterStartChar};
		}
		
		currCharIndex = nextCharIndex;
		currPos = nextPos;
	}

	// FIXME: Assert unreachable
	return {clusterStartChar};
}

uint32_t ParagraphLayout::get_first_run_index(size_t lineIndex) const {
	return lineIndex == 0 ? 0 : lines[lineIndex - 1].visualRunsEndIndex;
}

uint32_t ParagraphLayout::get_first_glyph_index(size_t runIndex) const {
	return runIndex == 0 ? 0 : visualRuns[runIndex - 1].glyphEndIndex;
}

uint32_t ParagraphLayout::get_first_position_index(size_t runIndex) const {
	return runIndex == 0 ? 0 : 2 * (visualRuns[runIndex - 1].glyphEndIndex + runIndex);
}

float ParagraphLayout::get_line_height(size_t lineIndex) const {
	return lineIndex == 0 ? lines.front().totalDescent
			: lines[lineIndex].totalDescent - lines[lineIndex - 1].totalDescent;
}

const float* ParagraphLayout::get_run_positions(size_t runIndex) const {
	return glyphPositions.data() + get_first_position_index(runIndex);
}

uint32_t ParagraphLayout::get_run_glyph_count(size_t runIndex) const {
	return visualRuns[runIndex].glyphEndIndex - get_first_glyph_index(runIndex);
}

float ParagraphLayout::get_glyph_offset_ltr(size_t runIndex, uint32_t cursor) const {
	auto firstGlyphIndex = get_first_glyph_index(runIndex);
	auto lastGlyphIndex = visualRuns[runIndex].glyphEndIndex;
	auto firstPosIndex = get_first_position_index(runIndex);

	float glyphOffset = 0.f;

	auto glyphIndex = binary_search(firstGlyphIndex, lastGlyphIndex - firstGlyphIndex, [&](auto index) {
		return charIndices[index] < cursor;
	});

	auto nextCharIndex = glyphIndex == lastGlyphIndex ? visualRuns[runIndex].charEndIndex
			: charIndices[glyphIndex];
	auto clusterDiff = nextCharIndex - cursor;

	glyphOffset = glyphPositions[firstPosIndex + 2 * (glyphIndex - firstGlyphIndex)];

	if (clusterDiff > 0 && glyphIndex > 0) {
		auto clusterCodeUnitCount = nextCharIndex - charIndices[glyphIndex - 1];
		auto prevGlyphOffset = glyphPositions[firstPosIndex + 2 * (glyphIndex - firstGlyphIndex - 1)];
		auto scaleFactor = static_cast<float>(clusterCodeUnitCount - clusterDiff)
				/ static_cast<float>(clusterCodeUnitCount);

		glyphOffset = prevGlyphOffset + (glyphOffset - prevGlyphOffset) * scaleFactor;
	}

	return glyphOffset;
}

float ParagraphLayout::get_glyph_offset_rtl(size_t runIndex, uint32_t cursor) const {
	auto firstGlyphIndex = get_first_glyph_index(runIndex);
	auto lastGlyphIndex = visualRuns[runIndex].glyphEndIndex;
	auto firstPosIndex = get_first_position_index(runIndex);

	float glyphOffset = 0.f;

	auto glyphIndex = binary_search(firstGlyphIndex, lastGlyphIndex - firstGlyphIndex, [&](auto index) {
		return charIndices[index] >= cursor;
	});

	auto nextCharIndex = glyphIndex == firstGlyphIndex ? visualRuns[runIndex].charEndIndex
			: charIndices[glyphIndex - 1];
	auto clusterDiff = nextCharIndex - cursor;

	glyphOffset = glyphPositions[firstPosIndex + 2 * (glyphIndex - firstGlyphIndex)];

	if (clusterDiff > 0 && glyphIndex < lastGlyphIndex) {
		auto clusterCodeUnitCount = nextCharIndex - charIndices[glyphIndex];
		auto prevGlyphOffset = glyphPositions[firstPosIndex + 2 * (glyphIndex - firstGlyphIndex + 1)];
		auto scaleFactor = static_cast<float>(clusterCodeUnitCount - clusterDiff)
				/ static_cast<float>(clusterCodeUnitCount);

		glyphOffset = prevGlyphOffset + (glyphOffset - prevGlyphOffset) * scaleFactor;
	}

	return glyphOffset;
}

// Build Paragraph Layout: ICU with icu::ParagraphLayout

void build_paragraph_layout_icu_lx(ParagraphLayout& result, const char16_t* chars, int32_t count,
		const Text::ValueRuns<const MultiScriptFont*>& fontRuns, float textAreaWidth, float textAreaHeight,
		TextYAlignment textYAlignment, ParagraphLayoutFlags flags) {
	Text::ValueRuns<const MultiScriptFont*> subsetFontRuns(fontRuns.get_value_count());

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
			size_t lastHighestRun{};
			int32_t highestRunCharEnd{INT32_MIN};

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

				auto firstParagraphRun = result.visualRuns.size();

				while (auto* pLine = pl.nextLine(textAreaWidth)) {
					handle_line_icu_lx(result, *pLine, byteIndex, chars, lastHighestRun, highestRunCharEnd);
					delete pLine;
				}
			}
			else {
				auto* pFont = fontRuns.get_value(byteIndex == count ? count - 1 : byteIndex);
				auto height = static_cast<float>(pFont->getAscent() + pFont->getDescent());

				lastHighestRun = result.visualRuns.size();
				highestRunCharEnd = byteIndex;

				// All inserted runs need at least 2 glyph position entries
				result.glyphPositions.emplace_back();
				result.glyphPositions.emplace_back();

				result.visualRuns.push_back({
					.glyphEndIndex = result.visualRuns.empty() ? 0 : result.visualRuns.back().glyphEndIndex,
					.charStartIndex = static_cast<uint32_t>(byteIndex),
					.charEndIndex = static_cast<uint32_t>(byteIndex),
				});

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

			result.visualRuns[lastHighestRun].charEndOffset = byteIndex - idx;
		}
	}

	auto totalHeight = result.lines.empty() ? 0.f : result.lines.back().totalDescent;
	result.textStartY = static_cast<float>(textYAlignment) * (textAreaHeight - totalHeight) * 0.5f;
}

static void handle_line_icu_lx(ParagraphLayout& result, icu::ParagraphLayout::Line& line,
		int32_t charOffset, const char16_t* chars, size_t& highestRun, int32_t& highestRunCharEnd) {
	result.visualRuns.reserve(result.visualRuns.size() + line.countRuns());

	int32_t maxAscent = 0;
	int32_t maxDescent = 0;

	for (int32_t i = 0; i < line.countRuns(); ++i) {
		auto* pRun = line.getVisualRun(i);
		auto ascent = pRun->getAscent();
		auto descent = pRun->getDescent();
		auto* pGlyphs = pRun->getGlyphs();
		auto* pGlyphPositions = pRun->getPositions();
		auto* pCharMap = pRun->getGlyphToCharMap();
		auto glyphCount = pRun->getGlyphCount();
		bool rightToLeft = pRun->getDirection() == UBIDI_RTL;

		if (ascent > maxAscent) {
			maxAscent = ascent;
		}

		if (descent > maxDescent) {
			maxDescent = descent;
		}

		for (int32_t j = 0; j < glyphCount; ++j) {
			if (pGlyphs[j] != 0xFFFF) {
				result.glyphs.emplace_back(pGlyphs[j]);
				result.charIndices.emplace_back(pCharMap[j] + charOffset);
				result.glyphPositions.emplace_back(pGlyphPositions[2 * j]);
				result.glyphPositions.emplace_back(pGlyphPositions[2 * j + 1]);
			}
		}

		result.glyphPositions.emplace_back(pGlyphPositions[2 * glyphCount]);
		result.glyphPositions.emplace_back(pGlyphPositions[2 * glyphCount + 1]);

		auto firstChar = pCharMap[0] + charOffset;
		auto lastChar = pCharMap[glyphCount - 1] + charOffset;

		if (rightToLeft) {
			std::swap(firstChar, lastChar);
		}

		U16_FWD_1_UNSAFE(chars, lastChar);

		if (lastChar > highestRunCharEnd) {
			highestRun = result.visualRuns.size();
			highestRunCharEnd = lastChar;
		}

		result.visualRuns.push_back({
			.pFont = static_cast<const Font*>(pRun->getFont()),
			.glyphEndIndex = static_cast<uint32_t>(result.glyphs.size()),
			.charStartIndex = static_cast<uint32_t>(firstChar),
			.charEndIndex = static_cast<uint32_t>(lastChar),
			.rightToLeft = rightToLeft,
		});
	}

	auto height = static_cast<float>(maxAscent + maxDescent);

	result.lines.push_back({
		.visualRunsEndIndex = static_cast<uint32_t>(result.visualRuns.size()),
		.width = static_cast<float>(line.getWidth()),
		.ascent = static_cast<float>(maxAscent),
		.totalDescent = result.lines.empty() ? height : result.lines.back().totalDescent + height,
	});
}

static bool affinity_prefer_prev_run(bool atLineBreak, bool atSoftLineBreak, bool prevRunRTL, bool nextRunRTL,
		CursorAffinity affinity) {
	// Case 1: Current run is at a soft line break
	return (atSoftLineBreak && affinity == CursorAffinity::OPPOSITE)
			// Case 2: Transition from RTL-LTR
			|| (!atLineBreak && prevRunRTL && !nextRunRTL && affinity == CursorAffinity::DEFAULT)
			// Case 3: Transition from LTR-RTL
			|| (!atLineBreak && !prevRunRTL && nextRunRTL && affinity == CursorAffinity::OPPOSITE);
}

