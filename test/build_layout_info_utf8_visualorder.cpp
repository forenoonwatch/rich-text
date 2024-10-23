#include "layout_info.hpp"

#include "binary_search.hpp"
#include "value_run_utils.hpp"
#include "font_registry.hpp"
#include "script_run_iterator.hpp"

#include <unicode/brkiter.h>
#include <unicode/casemap.h>
#include <unicode/edits.h>

extern "C" {
#include <SheenBidi.h>
}

#include <hb.h>

#include <cmath>

using namespace Text;

namespace {

struct LayoutBuildState {
	explicit LayoutBuildState()
			: pBuffer(hb_buffer_create()) {
		UErrorCode err{U_ZERO_ERROR};
		pLineBreakIterator = icu::BreakIterator::createLineInstance(icu::Locale::getDefault(), err);
		hb_buffer_set_cluster_level(pBuffer, HB_BUFFER_CLUSTER_LEVEL_MONOTONE_CHARACTERS);
	}

	~LayoutBuildState() {
		delete pLineBreakIterator;
		hb_buffer_destroy(pBuffer);
	}

	void reset(size_t capacity);

	icu::BreakIterator* pLineBreakIterator;
	hb_buffer_t* pBuffer;
	std::vector<uint32_t> glyphs;
	std::vector<uint32_t> charIndices;
	std::vector<uint32_t> charIndicesV;
	std::vector<float> glyphPositionsX;
	std::vector<float> glyphPositionsY;
	std::vector<int32_t> glyphWidths;

	int32_t cursorX;
	int32_t cursorY;
};

struct LogicalRun {
	SingleScriptFont font;
	const icu::Locale* pLocale;
	SBLevel level;
	UScriptCode script;
	int32_t charEndIndex;
	uint32_t glyphEndIndex;
};

struct LogicalRunIterator {
	const LogicalRun* pRuns;
	ptrdiff_t index;
	ptrdiff_t endIndex;
	ptrdiff_t dir;
	size_t runIndex;

	void advance() {
		index += dir;

		if (index == endIndex) {
			auto runStartIndex = pRuns[runIndex++].glyphEndIndex;
			bool rightToLeft = pRuns[runIndex].level & 1;
			index = rightToLeft ? static_cast<ptrdiff_t>(pRuns[runIndex].glyphEndIndex) - 1 : runStartIndex;
			endIndex = rightToLeft ? static_cast<ptrdiff_t>(runStartIndex) - 1 : pRuns[runIndex].glyphEndIndex;
			dir = rightToLeft ? -1 : 1;
		}
	}
};

}

static size_t build_sub_paragraph(LayoutBuildState& state, LayoutInfo& result, SBParagraphRef sbParagraph,
		const char* chars, int32_t count, int32_t stringOffset, const ValueRuns<Font>& fontRuns,
		const ValueRuns<bool>* pSmallcapsRuns, const ValueRuns<bool>* pSubscriptRuns,
		const ValueRuns<bool>* pSuperscriptRuns, int32_t fixedWidth);

static ValueRuns<SBLevel> compute_levels(SBParagraphRef sbParagraph, size_t paragraphLength);
static ValueRuns<UScriptCode> compute_scripts(const char* chars, int32_t count);
static ValueRuns<SingleScriptFont> compute_sub_fonts(const char* chars, const ValueRuns<Font>& fontRuns,
		const ValueRuns<UScriptCode>& scriptRuns, const ValueRuns<bool>& smallcapsRuns,
		const ValueRuns<bool>& subscriptRuns, const ValueRuns<bool>& superscriptRuns);

static void shape_logical_run(LayoutBuildState& state, const SingleScriptFont& font, const char* chars,
		int32_t offset, int32_t count, int32_t max, UScriptCode script, const icu::Locale& locale,
		bool rightToLeft, int32_t stringOffset);
static int32_t find_previous_line_break(icu::BreakIterator& iter, const char* chars, int32_t count,
		int32_t charIndex);
static void compute_line_visual_runs(LayoutBuildState& state, LayoutInfo& result,
		const std::vector<LogicalRun>& logicalRuns, SBParagraphRef sbParagraph,
		const char* chars, int32_t count, int32_t lineStart, int32_t lineEnd,  int32_t stringOffset,
		size_t& highestRun, int32_t& highestRunCharEnd);
static void append_visual_run(LayoutBuildState& state, LayoutInfo& result, const LogicalRun* logicalRuns,
		size_t logicalRunIndex, int32_t charStartIndex, int32_t charEndIndex, float& visualRunLastX,
		size_t& highestRun, int32_t& highestRunCharEnd);

// Public Functions

/**
 * ALGORITHM:
 * 1. Split the string by paragraph boundaries (hard line breaks) (UBA P1)
 * 2. For each paragraph:
 *   2a. Determine script runs (UAX #24)
 *   2b. Determine font runs based on text and script runs
 *   2c. Determine directional levels (UBA X1-I2)
 *   2d. Generate the set of Logical Runs, where each run represents a region over which the font, script,
 *       and level do not change. For each logical run, generate shaping data.
 *   2e. If the text area width > 0, accumulate glyph widths until the width overflows the line. Find the
 *       previous safe break point using the line break iterator. Otherwise, skip directly to 2f with the line
 *       range covering [paragraphStart, paragraphEnd]. (UBA 3.4)
 *   2f. For each range [lineStart, lineEnd] as computed in step 2e, compute the visual runs following the UBA
 *       (UBA L1-L2).
 *
 * UBA: https://unicode.org/reports/tr9
 * UAX #24: https://www.unicode.org/reports/tr24/
 *
 * DATA & ORDERING NOTES:
 * 1. The LayoutInfo struct expects all data to be in final visual order, both at the run and character level.
 * 2. HarfBuzz provides all data in visual order, within the context of the logical run (i.e. the data is
 *    reversed relative to the source string if requested as RTL).
 * 3. Line breaking (step 2e) requires positions and charIndices to be in logical order to calculate widths
 *    (UBA L1.1).
 * 4. Glyph mirroring (UBA L4) requires glyph index order to match char index order up to computing line
 *    visual runs.
 *
 * FIXME: The algorithm does not select mirror glyphs currently.
 */
void Text::build_layout_info_utf8_2(LayoutInfo& result, const char* chars, int32_t count,
		const ValueRuns<Font>& fontRuns, float textAreaWidth, float textAreaHeight,
		YAlignment textYAlignment, LayoutInfoFlags flags, const ValueRuns<bool>* pSmallcapsRuns,
		const ValueRuns<bool>* pSubscriptRuns, const ValueRuns<bool>* pSuperscriptRuns) {
	result.clear();

	LayoutBuildState state{};
	SBCodepointSequence codepointSequence{SBStringEncodingUTF8, (void*)chars, (size_t)count};
	SBAlgorithmRef sbAlgorithm = SBAlgorithmCreate(&codepointSequence);
	size_t paragraphOffset{};	

	// FIXME: Give the sub-paragraphs a full view of font runs
	ValueRuns<Font> subsetFontRuns(fontRuns.get_run_count());
	size_t lastHighestRun = 0;

	SBLevel baseDefaultLevel = ((flags & LayoutInfoFlags::RIGHT_TO_LEFT) == LayoutInfoFlags::NONE)
			? SBLevelDefaultLTR : SBLevelDefaultRTL;

	// 26.6 fixed-point text area width
	auto fixedTextAreaWidth = static_cast<int32_t>(textAreaWidth * 64.f);

	while (paragraphOffset < count) {
		size_t paragraphLength, separatorLength;
		SBAlgorithmGetParagraphBoundary(sbAlgorithm, paragraphOffset, INT32_MAX, &paragraphLength,
				&separatorLength);
		bool isLastParagraph = paragraphOffset + paragraphLength == count;

		if (paragraphLength - separatorLength > 0) {
			auto byteCount = paragraphLength - separatorLength * (!isLastParagraph);
			subsetFontRuns.clear();
			fontRuns.get_runs_subset(paragraphOffset, byteCount, subsetFontRuns);

			SBParagraphRef sbParagraph = SBAlgorithmCreateParagraph(sbAlgorithm, paragraphOffset,
					paragraphLength, baseDefaultLevel);
			lastHighestRun = build_sub_paragraph(state, result, sbParagraph, chars + paragraphOffset, byteCount,
					paragraphOffset, subsetFontRuns, pSmallcapsRuns, pSubscriptRuns, pSuperscriptRuns,
					fixedTextAreaWidth);
			SBParagraphRelease(sbParagraph);
		}
		else {
			auto font = fontRuns.get_value(paragraphOffset == count ? count - 1 : paragraphOffset);
			auto fontData = FontRegistry::get_font_data(font);
			auto height = fontData.get_ascent() - fontData.get_descent();

			lastHighestRun = result.get_run_count();
			result.append_empty_line(FontRegistry::get_default_single_script_font(font),
					static_cast<uint32_t>(paragraphOffset), height, fontData.get_ascent());
		}

		result.set_run_char_end_offset(lastHighestRun, separatorLength * (!isLastParagraph));

		paragraphOffset += paragraphLength;
	}

	auto totalHeight = result.get_text_height();
	result.set_text_start_y(static_cast<float>(textYAlignment) * (textAreaHeight - totalHeight) * 0.5f);

	SBAlgorithmRelease(sbAlgorithm);
}

// Static Functions

static bool find_width_of_run(const LayoutBuildState& state, size_t runStart, size_t runEnd,
		size_t nextRunStart, bool rightToLeft, int32_t textAreaWidth, int32_t& lineWidthSoFar, size_t& result) {
	if (rightToLeft) {
		auto lastPos = static_cast<int32_t>(state.glyphPositionsX[runEnd] * 64.f);

		for (; runEnd-- > runStart;) {
			auto pos = static_cast<int32_t>(state.glyphPositionsX[runEnd] * 64.f);
			auto width = lastPos - pos;
			lastPos = pos;

			if (lineWidthSoFar + width > textAreaWidth) {
				result = runEnd;
				return false;
			}

			lineWidthSoFar += width;
		}
	}
	else {
		auto lastPos = static_cast<int32_t>(state.glyphPositionsX[runStart] * 64.f);

		for (; runStart < runEnd; ++runStart) {
			auto pos = static_cast<int32_t>(state.glyphPositionsX[runStart + 1] * 64.f);
			auto width = pos - lastPos;
			lastPos = pos;

			if (lineWidthSoFar + width > textAreaWidth) {
				result = runStart;
				return false;
			}

			lineWidthSoFar += width;
		}
	}

	result = nextRunStart;
	return true;
}

static int32_t find_next_line_end(const LayoutBuildState& state, const std::vector<LogicalRun>& logicalRuns,
		int32_t textAreaWidth, int32_t lineStart, const char* chars, int32_t stringOffset, int32_t count) {
	auto runIndex = binary_search(0, logicalRuns.size(), [&](auto index) {
		return logicalRuns[index].charEndIndex <= lineStart - stringOffset;
	});

	int32_t lineWidthSoFar{};
	auto runStart = runIndex == 0 ? 0 : logicalRuns[runIndex - 1].glyphEndIndex;
	auto runEnd = logicalRuns[runIndex].glyphEndIndex;
	auto runSeparator = runStart;
	bool runRTL = logicalRuns[runIndex].level & 1;
	while (runSeparator < logicalRuns[runIndex].glyphEndIndex
			&& state.charIndicesV[runSeparator] != lineStart) {
		++runSeparator;
	}
	size_t glyphIndex{};

	if (find_width_of_run(state, runRTL ? runStart : runSeparator, runRTL ? runSeparator + 1 : runEnd,
			runEnd, runRTL, textAreaWidth, lineWidthSoFar, glyphIndex)) {
		++runIndex;

		while (runIndex < logicalRuns.size()) {
			runStart = runEnd;
			runEnd = logicalRuns[runIndex].glyphEndIndex;
			runRTL = logicalRuns[runIndex].level & 1;

			if (!find_width_of_run(state, runStart, runEnd, runEnd, runRTL, textAreaWidth, lineWidthSoFar,
					glyphIndex)) {
				break;
			}

			++runIndex;
		}
	}

	LogicalRunIterator iter{logicalRuns.data(), static_cast<ptrdiff_t>(glyphIndex),
			runRTL ? static_cast<ptrdiff_t>(runStart) - 1 : runEnd, runRTL ? -1 : 1,
			runIndex};

	if (lineWidthSoFar == 0 && iter.runIndex < logicalRuns.size()) {
		iter.advance();
	}

	auto charIndex = iter.runIndex < logicalRuns.size() ? state.charIndicesV[iter.index]
			: stringOffset + count;
	auto lineEnd = find_previous_line_break(*state.pLineBreakIterator, chars, count, charIndex - stringOffset)
			+ stringOffset;

	while (lineEnd <= lineStart && iter.runIndex < logicalRuns.size()) {
		lineEnd = state.charIndicesV[iter.index];
		iter.advance();
	}

	if (lineEnd <= lineStart) {
		lineEnd = stringOffset + count;
	}

	return lineEnd;
}

static size_t build_sub_paragraph(LayoutBuildState& state, LayoutInfo& result, SBParagraphRef sbParagraph,
		const char* chars, int32_t count, int32_t stringOffset, const ValueRuns<Font>& fontRuns,
		const ValueRuns<bool>* pSmallcapsRuns, const ValueRuns<bool>* pSubscriptRuns,
		const ValueRuns<bool>* pSuperscriptRuns, int32_t textAreaWidth) {
	auto levelRuns = compute_levels(sbParagraph, count);
	auto scriptRuns = compute_scripts(chars, count);
	ValueRuns<const icu::Locale*> localeRuns(&icu::Locale::getDefault(), count);

	// FIXME: Build a ValueRunsIterator that doesn't require building fake runs - also properly iterate
	// sub-sections of the parent runs
	ValueRuns<bool> smallcapsRuns(false, fontRuns.get_limit());
	ValueRuns<bool> subscriptRuns(false, fontRuns.get_limit());
	ValueRuns<bool> superscriptRuns(false, fontRuns.get_limit());

	if (pSmallcapsRuns) {
		smallcapsRuns.clear();
		pSmallcapsRuns->get_runs_subset(stringOffset, count, smallcapsRuns);
	}

	if (pSubscriptRuns) {
		subscriptRuns.clear();
		pSubscriptRuns->get_runs_subset(stringOffset, count, subscriptRuns);
	}

	if (pSuperscriptRuns) {
		superscriptRuns.clear();
		pSuperscriptRuns->get_runs_subset(stringOffset, count, superscriptRuns);
	}

	auto subFontRuns = compute_sub_fonts(chars, fontRuns, scriptRuns, smallcapsRuns, subscriptRuns,
			superscriptRuns);
	int32_t runStart{};

	std::vector<LogicalRun> logicalRuns;

	// Generate logical run definitions
	iterate_run_intersections([&](auto limit, auto font, auto level, auto script, auto* pLocale) {
		logicalRuns.push_back({
			.font = font,
			.pLocale = pLocale,
			.level = level,
			.script = script,
			.charEndIndex = limit,
		});
	}, subFontRuns, levelRuns, scriptRuns, localeRuns);

	state.reset(count);

	// Shape all logical runs
	for (auto& run : logicalRuns) {
		bool rightToLeft = run.level & 1;
		shape_logical_run(state, run.font, chars, runStart, run.charEndIndex - runStart, count, run.script,
				*run.pLocale, rightToLeft, stringOffset);
		run.glyphEndIndex = static_cast<uint32_t>(state.glyphs.size());
		runStart = run.charEndIndex;
	}

	// Finalize the last advance after the last character in the paragraph
	state.glyphPositionsX.emplace_back(scalbnf(state.cursorX, -6));
	state.glyphPositionsY.emplace_back(scalbnf(state.cursorY, -6));

	size_t highestRun{};
	int32_t highestRunCharEnd{INT32_MIN};

	// If width == 0, perform no line breaking
	if (textAreaWidth == 0) {
		compute_line_visual_runs(state, result, logicalRuns, sbParagraph, chars, count,
				stringOffset, stringOffset + count, stringOffset, highestRun, highestRunCharEnd);
		return highestRun;
	}

	// Find line breaks
	UText uText UTEXT_INITIALIZER;
	UErrorCode err{};
	utext_openUTF8(&uText, chars, count, &err);
	state.pLineBreakIterator->setText(&uText, err);

	int32_t lineEnd = stringOffset;
	int32_t lineStart;

	while (lineEnd < stringOffset + count) {
		lineStart = lineEnd;
		lineEnd = find_next_line_end(state, logicalRuns, textAreaWidth, lineStart, chars, stringOffset, count);

		compute_line_visual_runs(state, result, logicalRuns, sbParagraph, chars, count, lineStart, lineEnd,
				stringOffset, highestRun, highestRunCharEnd);
	}

	return highestRun;
}

static ValueRuns<SBLevel> compute_levels(SBParagraphRef sbParagraph, size_t paragraphLength) {
	ValueRuns<SBLevel> levelRuns;
	auto* levels = SBParagraphGetLevelsPtr(sbParagraph);
	SBLevel lastLevel = levels[0];
	size_t lastLevelCount{};

	for (size_t i = 1; i < paragraphLength; ++i) {
		if (levels[i] != lastLevel) {
			levelRuns.add(i, lastLevel);
			lastLevel = levels[i];
		}
	}

	levelRuns.add(paragraphLength, lastLevel);

	return levelRuns;
}

static ValueRuns<UScriptCode> compute_scripts(const char* chars, int32_t count) {
	ScriptRunIterator runIter(chars, count);
	ValueRuns<UScriptCode> scriptRuns;
	int32_t start;
	int32_t limit;
	UScriptCode script;

	while (runIter.next(start, limit, script)) {
		scriptRuns.add(limit, script);
	}

	return scriptRuns;
}

static ValueRuns<SingleScriptFont> compute_sub_fonts(const char* chars, const ValueRuns<Font>& fontRuns,
		const ValueRuns<UScriptCode>& scriptRuns, const ValueRuns<bool>& smallcapsRuns,
		const ValueRuns<bool>& subscriptRuns, const ValueRuns<bool>& superscriptRuns) {
	ValueRuns<SingleScriptFont> result(fontRuns.get_run_count());
	int32_t offset{};

	iterate_run_intersections([&](auto limit, auto baseFont, auto script, bool smallCaps, bool subscript,
			bool superscript) {
		while (offset < limit) {
			auto subFont = FontRegistry::get_sub_font(baseFont, chars, offset, limit, script, smallCaps,
					subscript, superscript);
			result.add(offset, subFont);
		}
	}, fontRuns, scriptRuns, smallcapsRuns, subscriptRuns, superscriptRuns);

	return result;
}

static void maybe_add_tag_runs(std::vector<hb_feature_t>& features, hb_tag_t tag, int32_t count,
		bool needsFeature, bool isSynthesizingThis) {
	if (!needsFeature || isSynthesizingThis) {
		return;
	}

	features.push_back({
		.tag = tag,
		.value = 1,
		.end = static_cast<unsigned>(count),
	});
}

static void remap_char_index(hb_glyph_info_t& glyphInfo, icu::Edits::Iterator& it, const char* sourceStr) {
	UErrorCode errc{};
	if (it.findSourceIndex(glyphInfo.cluster, errc)) [[likely]] {
		auto srcIndex = it.sourceIndex() + (glyphInfo.cluster - it.destinationIndex());

		// Sometimes the edits can map a codepoint lead to a trailing code unit in the source string,
		// e.g. latin sharp S -> SS
		while (U8_IS_TRAIL(sourceStr[srcIndex]) && srcIndex > 0) {
			--srcIndex;
		}

		glyphInfo.cluster = srcIndex;
	}
}

static void remap_char_indices(hb_glyph_info_t* glyphInfos, unsigned glyphCount, icu::Edits& edits,
		const char* sourceStr, bool rightToLeft) {
	auto it = edits.getFineChangesIterator();

	if (rightToLeft) {
		for (unsigned i = glyphCount; i--;) {
			remap_char_index(glyphInfos[i], it, sourceStr);
		}
	}
	else {
		for (unsigned i = 0; i < glyphCount; ++i) {
			remap_char_index(glyphInfos[i], it, sourceStr);
		}
	}
}

static void shape_logical_run(LayoutBuildState& state, const SingleScriptFont& font, const char* chars,
		int32_t offset, int32_t count, int32_t max, UScriptCode script, const icu::Locale& locale,
		bool rightToLeft, int32_t stringOffset) {
	hb_buffer_clear_contents(state.pBuffer);

	hb_buffer_set_script(state.pBuffer, hb_script_from_string(uscript_getShortName(script), 4));
	hb_buffer_set_language(state.pBuffer, hb_language_from_string(locale.getLanguage(), -1));
	hb_buffer_set_direction(state.pBuffer, rightToLeft ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
	hb_buffer_set_flags(state.pBuffer, (hb_buffer_flags_t)((offset == 0 ? HB_BUFFER_FLAG_BOT : 0)
			| (offset + count == max ? HB_BUFFER_FLAG_EOT : 0)));

	icu::Edits edits;

	if (font.syntheticSmallCaps) {
		std::string upperStr;
		icu::StringByteSink<std::string> sink(&upperStr);
		UErrorCode errc{};

		icu::CaseMap::utf8ToUpper(uscript_getName(script), 0, {chars + offset, count}, sink, &edits, errc);
		hb_buffer_add_utf8(state.pBuffer, upperStr.data(), (int)upperStr.size(), 0, (int)upperStr.size());
		count = (int32_t)upperStr.size();
	}
	else {
		hb_buffer_add_utf8(state.pBuffer, chars, max, offset, 0);
		hb_buffer_add_utf8(state.pBuffer, chars + offset, max - offset, 0, count);
	}

	std::vector<hb_feature_t> features;
	maybe_add_tag_runs(features, HB_TAG('s', 'm', 'c', 'p'), count, font.smallcaps, font.syntheticSmallCaps);
	maybe_add_tag_runs(features, HB_TAG('s', 'u', 'b', 's'), count, font.subscript, font.syntheticSubscript);
	maybe_add_tag_runs(features, HB_TAG('s', 'u', 'p', 's'), count, font.superscript,
			font.syntheticSuperscript);

	auto fontData = FontRegistry::get_font_data(font);
	hb_shape(fontData.hbFont, state.pBuffer, features.data(), static_cast<unsigned>(features.size()));

	auto glyphCount = hb_buffer_get_length(state.pBuffer);
	auto* glyphPositions = hb_buffer_get_glyph_positions(state.pBuffer, nullptr);
	auto* glyphInfos = hb_buffer_get_glyph_infos(state.pBuffer, nullptr);

	if (font.syntheticSmallCaps) {
		remap_char_indices(glyphInfos, glyphCount, edits, chars + offset, rightToLeft);
	}

	for (unsigned i = 0; i < glyphCount; ++i) {
		state.charIndicesV.emplace_back(glyphInfos[i].cluster + offset + stringOffset);
		state.glyphPositionsX.emplace_back(scalbnf(state.cursorX + glyphPositions[i].x_offset, -6));
		state.glyphPositionsY.emplace_back(scalbnf(state.cursorY + glyphPositions[i].y_offset, -6));
		state.cursorX += glyphPositions[i].x_advance;
		state.cursorY += glyphPositions[i].y_advance;
	}

	if (rightToLeft) {
		for (unsigned i = glyphCount; i--;) {
			state.glyphs.emplace_back(glyphInfos[i].codepoint);
			state.charIndices.emplace_back(glyphInfos[i].cluster + offset + stringOffset);

			if (i == glyphCount - 1) {
				state.glyphWidths.emplace_back(glyphPositions[i].x_advance - glyphPositions[i].x_offset);
			}
			else {
				state.glyphWidths.emplace_back(glyphPositions[i].x_advance + glyphPositions[i + 1].x_offset
						- glyphPositions[i].x_offset);
			}
		}
	}
	else {
		for (unsigned i = 0; i < glyphCount; ++i) {
			state.glyphs.emplace_back(glyphInfos[i].codepoint);
			state.charIndices.emplace_back(glyphInfos[i].cluster + offset + stringOffset);

			if (i == glyphCount - 1) {
				state.glyphWidths.emplace_back(glyphPositions[i].x_advance - glyphPositions[i].x_offset);
			}
			else {
				state.glyphWidths.emplace_back(glyphPositions[i].x_advance + glyphPositions[i + 1].x_offset
						- glyphPositions[i].x_offset);
			}
		}
	}
}

static int32_t find_previous_line_break(icu::BreakIterator& iter, const char* chars, int32_t count,
		int32_t charIndex) {
	// Skip over any whitespace or control characters because they can hang in the margin
	UChar32 chr;
	while (charIndex < count) {
		// FIXME: U8_GET may be O(n), use U8_NEXT instead
		U8_GET((const uint8_t*)chars, 0, charIndex, count, chr);

		if (!u_isWhitespace(chr) && !u_iscntrl(chr)) {
			break;
		}

		U8_FWD_1(chars, charIndex, count);
	}

	// Return the break location that's at or before the character we stopped on. Note: if we're on a break, the
	// `U8_FWD_1` will cause `preceding` to back up to it.
	U8_FWD_1(chars, charIndex, count);

	return iter.preceding(charIndex);
}

static void compute_line_visual_runs(LayoutBuildState& state, LayoutInfo& result,
		const std::vector<LogicalRun>& logicalRuns, SBParagraphRef sbParagraph, const char* chars,
		int32_t count, int32_t lineStart, int32_t lineEnd, int32_t stringOffset, size_t& highestRun,
		int32_t& highestRunCharEnd) {
	SBLineRef sbLine = SBParagraphCreateLine(sbParagraph, lineStart, lineEnd - lineStart);
	auto runCount = SBLineGetRunCount(sbLine);
	auto* sbRuns = SBLineGetRunsPtr(sbLine);
	float maxAscent{};
	float maxDescent{};
	float visualRunLastX{};

	for (int32_t i = 0; i < runCount; ++i) {
		int32_t logicalStart, runLength;
		bool rightToLeft = sbRuns[i].level & 1;
		auto runStart = sbRuns[i].offset - stringOffset;
		auto runEnd = runStart + sbRuns[i].length - 1;

		if (!rightToLeft) {
			auto run = binary_search(0, logicalRuns.size(), [&](auto index) {
				return logicalRuns[index].charEndIndex <= runStart;
			});
			auto chrIndex = runStart;

			for (;;) {
				auto logicalRunEnd = logicalRuns[run].charEndIndex;
				auto fontData = FontRegistry::get_font_data(logicalRuns[run].font);

				if (auto ascent = fontData.get_ascent(); ascent > maxAscent) {
					maxAscent = ascent;
				}

				if (auto descent = fontData.get_descent(); descent < maxDescent) {
					maxDescent = descent;
				}

				if (runEnd < logicalRunEnd) {
					append_visual_run(state, result, logicalRuns.data(), run, chrIndex + stringOffset,
							runEnd + stringOffset, visualRunLastX, highestRun, highestRunCharEnd);
					break;
				}
				else {
					append_visual_run(state, result, logicalRuns.data(), run, chrIndex + stringOffset,
							logicalRunEnd - 1 + stringOffset, visualRunLastX, highestRun, highestRunCharEnd);
					chrIndex = logicalRunEnd;
					++run;
				}
			}
		}
		else {
			auto run = binary_search(0, logicalRuns.size(), [&](auto index) {
				return logicalRuns[index].charEndIndex <= runEnd;
			});
			auto chrIndex = runEnd;

			for (;;) {
				auto logicalRunStart = run == 0 ? 0 : logicalRuns[run - 1].charEndIndex;
				auto fontData = FontRegistry::get_font_data(logicalRuns[run].font);

				if (auto ascent = fontData.get_ascent(); ascent > maxAscent) {
					maxAscent = ascent;
				}

				if (auto descent = fontData.get_descent(); descent < maxDescent) {
					maxDescent = descent;
				}

				if (runStart >= logicalRunStart) {
					append_visual_run(state, result, logicalRuns.data(), run, runStart + stringOffset,
							chrIndex + stringOffset, visualRunLastX, highestRun, highestRunCharEnd);
					break;
				}
				else {
					append_visual_run(state, result, logicalRuns.data(), run, logicalRunStart + stringOffset,
							chrIndex + stringOffset, visualRunLastX, highestRun, highestRunCharEnd);
					chrIndex = logicalRunStart - 1;
					--run;
				}
			}
		}
	}

	result.append_line(maxAscent - maxDescent, maxAscent);
	
	SBLineRelease(sbLine);
}

static void append_visual_run(LayoutBuildState& state, LayoutInfo& result, const LogicalRun* logicalRuns,
		size_t run, int32_t charStartIndex, int32_t charEndIndex, float& visualRunLastX, size_t& highestRun,
		int32_t& highestRunCharEnd) {
	auto logicalFirstGlyph = run == 0 ? 0 : logicalRuns[run - 1].glyphEndIndex;
	auto logicalLastGlyph = logicalRuns[run].glyphEndIndex;
	bool rightToLeft = logicalRuns[run].level & 1;
	uint32_t visualFirstGlyph;
	uint32_t visualLastGlyph;

	if (charEndIndex > highestRunCharEnd) {
		highestRun = result.get_run_count();
		highestRunCharEnd = charEndIndex;
	}

	visualFirstGlyph = binary_search(logicalFirstGlyph, logicalLastGlyph - logicalFirstGlyph, [&](auto index) {
		return state.charIndices[index] < charStartIndex;
	});

	visualLastGlyph = binary_search(visualFirstGlyph, logicalLastGlyph - visualFirstGlyph, [&](auto index) {
		return state.charIndices[index] <= charEndIndex;
	});

	uint32_t vfg2{logicalFirstGlyph}, vlg2{logicalLastGlyph};

	for (uint32_t i = logicalFirstGlyph; i < logicalLastGlyph; ++i) {
		if (state.charIndices[i] == charStartIndex) {
			vfg2 = i;
			break;
		}
	}

	for (uint32_t i = logicalLastGlyph; i-- > vfg2;) {
		if (state.charIndices[i] <= charEndIndex) {
			vlg2 = i + 1;
			break;
		}
	}

	if (vfg2 != visualFirstGlyph || vlg2 != visualLastGlyph) {
		printf("fg %d %d | lg %d %d | lrr{%d-%d}, vrr{%d-%d}\n",
				visualFirstGlyph, vfg2, visualLastGlyph, vlg2,
				state.charIndices[logicalFirstGlyph], state.charIndices[logicalLastGlyph - 1],
				charStartIndex, charEndIndex);
	}

	uint32_t visualFirstPosIndex;
	uint32_t visualLastPosIndex;

	if (rightToLeft) {
		for (uint32_t i = visualLastGlyph; i-- > visualFirstGlyph;) {
			result.append_glyph(state.glyphs[i]);
			result.append_char_index(state.charIndices[i]);
		}

		visualFirstPosIndex = logicalFirstGlyph + logicalLastGlyph - visualLastGlyph;
		visualLastPosIndex = logicalFirstGlyph + logicalLastGlyph - visualFirstGlyph;
	}
	else {
		for (uint32_t i = visualFirstGlyph; i < visualLastGlyph; ++i) {
			result.append_glyph(state.glyphs[i]);
			result.append_char_index(state.charIndices[i]);
		}

		visualFirstPosIndex = logicalFirstGlyph + visualFirstGlyph - logicalFirstGlyph;
		visualLastPosIndex = logicalFirstGlyph + visualLastGlyph - logicalFirstGlyph;
	}

	visualRunLastX -= state.glyphPositionsX[visualFirstPosIndex];

	for (uint32_t i = visualFirstPosIndex; i < visualLastPosIndex; ++i) {
		result.append_glyph_position(state.glyphPositionsX[i] + visualRunLastX, state.glyphPositionsY[i]);
	}

	result.append_glyph_position(state.glyphPositionsX[visualLastPosIndex] + visualRunLastX,
			state.glyphPositionsY[visualLastPosIndex]);

	visualRunLastX += state.glyphPositionsX[visualLastPosIndex];

	result.append_run(logicalRuns[run].font, static_cast<uint32_t>(charStartIndex),
			static_cast<uint32_t>(charEndIndex + 1), rightToLeft);
}

// LayoutBuildState

void LayoutBuildState::reset(size_t capacity) {
	glyphs.clear();
	glyphs.reserve(capacity);

	charIndices.clear();
	charIndices.reserve(capacity);

	charIndicesV.clear();
	charIndicesV.reserve(capacity);

	glyphPositionsX.clear();
	glyphPositionsX.reserve(capacity + 1);
	glyphPositionsY.clear();
	glyphPositionsY.reserve(capacity + 1);

	glyphWidths.clear();
	glyphWidths.reserve(capacity);

	cursorX = 0;
	cursorY = 0;
}

