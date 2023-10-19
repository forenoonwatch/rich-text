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

static constexpr CursorPosition make_cursor(uint32_t position, CursorAffinity affinity) {
	return {position | (static_cast<uint32_t>(affinity) << 31)};
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
	auto firstGlyphIndex = get_first_glyph_index(runIndex);
	auto lastGlyphIndex = visualRuns[runIndex].glyphEndIndex;
	auto firstPosIndex = get_first_position_index(runIndex);
	auto lineX = get_line_x_start(lineIndex, textWidth, textXAlignment);

	float glyphOffset = 0.f;

	if (!is_empty_line(lineIndex)) {
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

	auto firstRunIndex = get_first_run_index(outLineNumber);
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
		bool atLineBreakStart = outLineNumber > 0 && lines[outLineNumber - 1].visualRunsEndIndex == runIndex;
		bool atSoftLineBreakStart = atLineBreakStart && lines[outLineNumber - 1].lastCharDiff == 0;

		// Case 1: Current run is at a soft line break
		if (atSoftLineBreakStart) {
			if (cursor.get_affinity() == CursorAffinity::OPPOSITE) {
				--outLineNumber;
				--runIndex;
			}
		}
		// Case 2: Transition from RTL-LTR
		else if (!atLineBreakStart && prevRun.rightToLeft && !run.rightToLeft) {
			if (cursor.get_affinity() == CursorAffinity::DEFAULT) {
				--runIndex;
			}
		}
		// Case 3: Transition from LTR-RTL
		else if (!atLineBreakStart && !prevRun.rightToLeft && run.rightToLeft) {
			if (cursor.get_affinity() == CursorAffinity::OPPOSITE) {
				--runIndex;
			}
		}
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

CursorPosition ParagraphLayout::get_line_start_position(size_t lineIndex) const {
	return {lineIndex == 0 ? 0 : lines[lineIndex - 1].lastStringIndex};
}

CursorPosition ParagraphLayout::get_line_end_position(size_t lineIndex) const {
	return {lines[lineIndex].lastStringIndex - lines[lineIndex].lastCharDiff};
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
	if (is_empty_line(lineNumber)) {
		return {lineNumber == 0 ? 0 : lines[lineNumber - 1].lastStringIndex};
	}

	auto& line = lines[lineNumber];
	cursorX -= get_line_x_start(lineNumber, textWidth, textXAlignment);

	// Find run containing char
	auto firstRunIndex = get_first_run_index(lineNumber);
	auto lastRunIndex = line.visualRunsEndIndex;
	auto runIndex = binary_search(firstRunIndex, lastRunIndex - firstRunIndex, [&](auto index) {
		auto firstPosIndex = index == 0 ? 0 : 2 * (visualRuns[index - 1].glyphEndIndex + index);
		auto lastPosIndex = 2 * (visualRuns[index].glyphEndIndex + index);
		auto rightmostIndex = visualRuns[index].rightToLeft ? firstPosIndex : lastPosIndex;
		return glyphPositions[rightmostIndex] < cursorX;
	});

	if (runIndex == lastRunIndex) {
		return {line.lastStringIndex - line.lastCharDiff};
	}

	// Find glyph in run
	auto firstGlyphIndex = get_first_glyph_index(runIndex);
	auto lastGlyphIndex = visualRuns[runIndex].glyphEndIndex;
	auto glyphIndex = lastGlyphIndex;
	auto firstPosIndex = get_first_position_index(runIndex);

	if (visualRuns[runIndex].rightToLeft) {
		glyphIndex = firstGlyphIndex + binary_search(0, lastGlyphIndex - firstGlyphIndex, [&](auto index) {
			return cursorX < glyphPositions[firstPosIndex + 2 * index];
		});
	}
	else {
		glyphIndex = firstGlyphIndex + binary_search(0, lastGlyphIndex - firstGlyphIndex, [&](auto index) {
			return glyphPositions[firstPosIndex + 2 * index] < cursorX;
		});
	}

	auto charIndex = glyphIndex == lastGlyphIndex && runIndex == lines[lineNumber].visualRunsEndIndex - 1
			? lines[lineNumber].lastStringIndex - lines[lineNumber].lastCharDiff
			: charIndices[glyphIndex];

	// Find final char position
	if (glyphIndex > firstGlyphIndex) {
		auto firstCharIndex = charIndices[glyphIndex - 1];
		auto prevCharIndex = iter.preceding(charIndex);

		auto firstCharPos = glyphPositions[firstPosIndex + 2 * (glyphIndex - 1 - firstGlyphIndex)];
		auto charPos = glyphPositions[firstPosIndex + 2 * (glyphIndex - firstGlyphIndex)];

		bool lastRunInLine = runIndex == lines[lineNumber].visualRunsEndIndex - 1;
		bool atSoftLineBreak = lastRunInLine && lines[lineNumber].lastCharDiff == 0;
		bool affinity = glyphIndex == lastGlyphIndex && (atSoftLineBreak
				|| (!lastRunInLine && !visualRuns[runIndex].rightToLeft
				&& visualRuns[runIndex + 1].rightToLeft));

		// Non-clustered glyph
		if (prevCharIndex == firstCharIndex) {
			// Choose index at closest side
			bool preferCharIndex = (charPos - cursorX < cursorX - firstCharPos)
					!= visualRuns[runIndex].rightToLeft;
			return preferCharIndex ? make_cursor(charIndex, affinity ? CursorAffinity::OPPOSITE
					: CursorAffinity::DEFAULT) : make_cursor(prevCharIndex, CursorAffinity::DEFAULT);
		}

		// Clustered glyph
		auto charStep = (firstCharPos - charPos) / static_cast<float>(charIndex - firstCharIndex);

		auto prevCharPos = charPos;
		charPos += charStep * static_cast<float>(charIndex - prevCharIndex);

		for (;;) {
			if (cursorX == charPos || ((cursorX > charPos) != visualRuns[runIndex].rightToLeft)) {
				// Choose index at closest side
				bool preferCharIndex = (prevCharPos - cursorX < cursorX - charPos)
						!= visualRuns[runIndex].rightToLeft;
				return preferCharIndex ? make_cursor(charIndex, charIndex == charIndices[glyphIndex] && affinity
						? CursorAffinity::OPPOSITE : CursorAffinity::DEFAULT) : make_cursor(prevCharIndex,
						CursorAffinity::DEFAULT);
			}

			if (firstCharIndex >= prevCharIndex) {
				return {firstCharIndex};
			}

			charIndex = prevCharIndex;
			prevCharIndex = iter.preceding(prevCharIndex);
			prevCharPos = charPos;
			charPos += charStep * static_cast<float>(charIndex - prevCharIndex);
		}
	}

	// FIXME: Assert unreachable
	return {charIndex};
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

bool ParagraphLayout::is_empty_line(size_t lineIndex) const {
	return lines[lineIndex].visualRunsEndIndex == 0 || (lineIndex > 0 && lines[lineIndex - 1].visualRunsEndIndex
			== lines[lineIndex].visualRunsEndIndex);
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

