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

struct LogicalRun {
	SingleScriptFont font;
	SBLevel level;
	int32_t charEndIndex;
	uint32_t glyphEndIndex;
};

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
	// Glyph positions are stored as 26.6 fixed point values, always in logical order. On the primary axis,
	// positions are stored as glyph widths. On the secondary axis, positions are stored in absolute position
	// as calculated from the offsets and advances.
	std::vector<int32_t> glyphPositions[2];

	int32_t cursorX;
	int32_t cursorY;

	std::vector<LogicalRun> logicalRuns;
};

class ScriptRunValueIterator {
	public:
		explicit ScriptRunValueIterator(const char* paragraphText, int32_t paragraphStart,
					int32_t paragraphLength)
				: m_iter(paragraphText, paragraphLength)
				, m_paragraphStart(paragraphStart) {
			int32_t start;
			m_iter.next(start, m_limit, m_value);
		}

		int32_t get_limit() const { return m_limit + m_paragraphStart; }
		UScriptCode get_value() const { return m_value; }

		void advance_to(int32_t index) {
			if (m_limit <= index) {
				int32_t start;
				m_iter.next(start, m_limit, m_value);
			}
		}
	private:
		ScriptRunIterator m_iter;
		int32_t m_paragraphStart;
		int32_t m_limit{};
		UScriptCode m_value{};
};

class LevelsIterator {
	public:
		explicit LevelsIterator(SBParagraphRef sbParagraph, int32_t paragraphStart, int32_t paragraphLength)
				: m_levels(SBParagraphGetLevelsPtr(sbParagraph))
				, m_end(m_levels + paragraphLength)
				, m_lastLevel(m_levels[0])
				, m_index(paragraphStart) {
			while (m_levels != m_end && *m_levels == m_lastLevel) {
				++m_levels;
				++m_index;
			}
		}

		SBLevel get_value() const { return m_lastLevel; }
		int32_t get_limit() const { return m_index; }

		void advance_to(int32_t index) {
			while (m_levels != m_end && m_index <= index) {
				m_lastLevel = *m_levels;

				while (m_levels != m_end && m_lastLevel == *m_levels) {
					++m_levels;
					++m_index;
				}
			}
		}
	private:
		const SBLevel* m_levels;
		const SBLevel* m_end;
		SBLevel m_lastLevel;
		int32_t m_index;
};

}

static size_t build_sub_paragraph(LayoutBuildState& state, LayoutInfo& result, SBParagraphRef sbParagraph,
		const char* fullText, int32_t paragraphLength, int32_t paragraphStart,
		ValueRunsIterator<Font>& itFont, MaybeDefaultRunsIterator<bool>& itSmallcaps,
		MaybeDefaultRunsIterator<bool>& itSubscript, MaybeDefaultRunsIterator<bool>& itSuperscript,
		int32_t fixedWidth, const icu::Locale& defaultLocale);

static void shape_logical_run(LayoutBuildState& state, const SingleScriptFont& font, const char* paragraphText,
		int32_t offset, int32_t count, int32_t paragraphStart, int32_t paragraphLength, UScriptCode script,
		const icu::Locale& locale, bool rightToLeft);
static int32_t find_previous_line_break(icu::BreakIterator& iter, const char* chars, int32_t count,
		int32_t charIndex);
static void compute_line_visual_runs(LayoutBuildState& state, LayoutInfo& result, SBParagraphRef sbParagraph,
		const char* chars, int32_t count, int32_t lineStart, int32_t lineEnd, int32_t stringOffset,
		size_t& highestRun, int32_t& highestRunCharEnd);
static void append_visual_run(LayoutBuildState& state, LayoutInfo& result, size_t logicalRunIndex,
		int32_t charStartIndex, int32_t charEndIndex, int32_t& visualRunLastX, size_t& highestRun,
		int32_t& highestRunCharEnd);

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

	ValueRunsIterator itFont(fontRuns);
	MaybeDefaultRunsIterator itSmallcaps(pSmallcapsRuns, false, count);
	MaybeDefaultRunsIterator itSubscript(pSubscriptRuns, false, count);
	MaybeDefaultRunsIterator itSuperscript(pSuperscriptRuns, false, count);

	size_t lastHighestRun = 0;

	SBLevel baseDefaultLevel = ((flags & LayoutInfoFlags::RIGHT_TO_LEFT) == LayoutInfoFlags::NONE)
			? SBLevelDefaultLTR : SBLevelDefaultRTL;

	// 26.6 fixed-point text area width
	auto fixedTextAreaWidth = static_cast<int32_t>(textAreaWidth * 64.f);

	auto& locale = icu::Locale::getDefault();

	while (paragraphOffset < count) {
		size_t paragraphLength, separatorLength;
		SBAlgorithmGetParagraphBoundary(sbAlgorithm, paragraphOffset, INT32_MAX, &paragraphLength,
				&separatorLength);
		bool isLastParagraph = paragraphOffset + paragraphLength == count;

		if (paragraphLength - separatorLength > 0) {
			auto byteCount = paragraphLength - separatorLength * (!isLastParagraph);

			SBParagraphRef sbParagraph = SBAlgorithmCreateParagraph(sbAlgorithm, paragraphOffset,
					paragraphLength, baseDefaultLevel);
			lastHighestRun = build_sub_paragraph(state, result, sbParagraph, chars, byteCount,
					paragraphOffset, itFont, itSmallcaps, itSubscript, itSuperscript, fixedTextAreaWidth,
					locale);
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

static size_t build_sub_paragraph(LayoutBuildState& state, LayoutInfo& result, SBParagraphRef sbParagraph,
		const char* fullText, int32_t paragraphLength, int32_t paragraphStart, ValueRunsIterator<Font>& itFont,
		MaybeDefaultRunsIterator<bool>& itSmallcaps, MaybeDefaultRunsIterator<bool>& itSubscript,
		MaybeDefaultRunsIterator<bool>& itSuperscript, int32_t textAreaWidth,
		const icu::Locale& defaultLocale) {
	const char* paragraphText = fullText + paragraphStart;
	auto paragraphEnd = paragraphStart + paragraphLength;

	state.reset(paragraphLength);

	// Generate logical run definitions and shape all logical runs
	LevelsIterator itLevels(sbParagraph, paragraphStart, paragraphLength);
	ScriptRunValueIterator itScripts(paragraphText, paragraphStart, paragraphLength);
	auto subFontOffset = paragraphStart;

	iterate_run_intersections(paragraphStart, paragraphEnd, [&](auto limit, auto baseFont, auto script,
			auto level, bool smallcaps, bool subscript, bool superscript) {
		while (subFontOffset < limit) {
			auto runStart = subFontOffset;
			auto subFont = FontRegistry::get_sub_font(baseFont, fullText, subFontOffset, limit,
					script, smallcaps, subscript, superscript);

			shape_logical_run(state, subFont, paragraphText, runStart - paragraphStart,
					subFontOffset - runStart, paragraphStart, paragraphLength, script, defaultLocale,
					level & 1);

			state.logicalRuns.push_back({
				.font = subFont,
				.level = level,
				.charEndIndex = subFontOffset - paragraphStart,
				.glyphEndIndex = static_cast<uint32_t>(state.glyphs.size()),
			});
		}
	}, itFont, itScripts, itLevels, itSmallcaps, itSubscript, itSuperscript);

	// Finalize the last advance after the last character in the paragraph
	state.glyphPositions[1].emplace_back(state.cursorY);

	size_t highestRun{};
	int32_t highestRunCharEnd{INT32_MIN};

	// If width == 0, perform no line breaking
	if (textAreaWidth == 0) {
		compute_line_visual_runs(state, result, sbParagraph, paragraphText, paragraphLength, paragraphStart,
				paragraphStart + paragraphLength, paragraphStart, highestRun, highestRunCharEnd);
		return highestRun;
	}

	// Find line breaks
	UText uText UTEXT_INITIALIZER;
	UErrorCode err{};
	utext_openUTF8(&uText, paragraphText, paragraphLength, &err);
	state.pLineBreakIterator->setText(&uText, err);

	int32_t lineEnd = paragraphStart;
	int32_t lineStart;

	while (lineEnd < paragraphStart + paragraphLength) {
		int32_t lineWidthSoFar{};

		lineStart = lineEnd;

		auto glyphIndex = binary_search(0, state.charIndices.size(), [&](auto index) {
			return state.charIndices[index] < lineStart;
		});

		while (glyphIndex < state.glyphs.size()
				&& lineWidthSoFar + state.glyphPositions[0][glyphIndex] <= textAreaWidth) {
			lineWidthSoFar += state.glyphPositions[0][glyphIndex];
			++glyphIndex;
		}

		// If no glyphs fit on the line, force one to fit. There shouldn't be any zero width glyphs at the start
		// of a line unless the paragraph consists of only zero width glyphs, because otherwise the zero width
		// glyphs will have been included on the end of the previous line
		if (lineWidthSoFar == 0 && glyphIndex < state.glyphs.size()) {
			++glyphIndex;
		}

		auto charIndex = glyphIndex == state.glyphs.size() ? paragraphLength + paragraphStart
				: state.charIndices[glyphIndex];
		lineEnd = find_previous_line_break(*state.pLineBreakIterator, paragraphText, paragraphLength,
				charIndex - paragraphStart) + paragraphStart;

		// If this break is at or before the last one, find a glyph that produces a break after the last one,
		// starting at the one which didn't fit
		while (lineEnd <= lineStart && glyphIndex < state.glyphs.size()) {
			lineEnd = state.charIndices[glyphIndex++];
		}

		if (lineEnd <= lineStart && glyphIndex == state.glyphs.size()) {
			lineEnd = paragraphStart + paragraphLength;
		}

		compute_line_visual_runs(state, result, sbParagraph, paragraphText, paragraphLength, lineStart, lineEnd,
				paragraphStart, highestRun, highestRunCharEnd);
	}

	return highestRun;
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

static void shape_logical_run(LayoutBuildState& state, const SingleScriptFont& font, const char* paragraphText,
		int32_t offset, int32_t count, int32_t paragraphStart, int32_t paragraphLength, UScriptCode script,
		const icu::Locale& locale, bool rightToLeft) {
	hb_buffer_clear_contents(state.pBuffer);

	hb_buffer_set_script(state.pBuffer, hb_script_from_string(uscript_getShortName(script), 4));
	hb_buffer_set_language(state.pBuffer, hb_language_from_string(locale.getLanguage(), -1));
	hb_buffer_set_direction(state.pBuffer, rightToLeft ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
	hb_buffer_set_flags(state.pBuffer, (hb_buffer_flags_t)((offset == 0 ? HB_BUFFER_FLAG_BOT : 0)
			| (offset + count == paragraphLength ? HB_BUFFER_FLAG_EOT : 0)));

	icu::Edits edits;

	if (font.syntheticSmallCaps) {
		std::string upperStr;
		icu::StringByteSink<std::string> sink(&upperStr);
		UErrorCode errc{};

		// FIXME: To produce accurate shaping results, harfbuzz needs +-5 characters around the substring,
		// if available. These should be provided within the upperStr
		icu::CaseMap::utf8ToUpper(uscript_getName(script), 0, {paragraphText + offset, count}, sink, &edits,
				errc);
		hb_buffer_add_utf8(state.pBuffer, upperStr.data(), (int)upperStr.size(), 0, (int)upperStr.size());
		count = (int32_t)upperStr.size();
	}
	else {
		hb_buffer_add_utf8(state.pBuffer, paragraphText, paragraphLength, offset, 0);
		hb_buffer_add_utf8(state.pBuffer, paragraphText + offset, paragraphLength - offset, 0, count);
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
		remap_char_indices(glyphInfos, glyphCount, edits, paragraphText + offset, rightToLeft);
	}

	auto glyphPosStartIndex = state.glyphPositions[1].size();

	for (unsigned i = 0; i < glyphCount; ++i) {
		state.glyphPositions[1].emplace_back(state.cursorY + glyphPositions[i].y_offset);
		state.cursorY += glyphPositions[i].y_advance;
	}

	if (rightToLeft) {
		for (unsigned i = glyphCount; i--;) {
			state.glyphs.emplace_back(glyphInfos[i].codepoint);
			state.charIndices.emplace_back(glyphInfos[i].cluster + offset + paragraphStart);

			auto width = i == glyphCount - 1
					? glyphPositions[i].x_advance - glyphPositions[i].x_offset
					: glyphPositions[i].x_advance + glyphPositions[i + 1].x_offset - glyphPositions[i].x_offset;
			state.glyphPositions[0].emplace_back(width);
		}

		std::reverse(state.glyphPositions[1].begin() + glyphPosStartIndex, state.glyphPositions[1].end());
	}
	else {
		for (unsigned i = 0; i < glyphCount; ++i) {
			state.glyphs.emplace_back(glyphInfos[i].codepoint);
			state.charIndices.emplace_back(glyphInfos[i].cluster + offset + paragraphStart);

			auto width = i == glyphCount - 1
					? glyphPositions[i].x_advance - glyphPositions[i].x_offset
					: glyphPositions[i].x_advance + glyphPositions[i + 1].x_offset - glyphPositions[i].x_offset;
			state.glyphPositions[0].emplace_back(width);
		}
	}
}

static int32_t find_previous_line_break(icu::BreakIterator& iter, const char* chars, int32_t count,
		int32_t charIndex) {
	// Skip over any whitespace or control characters because they can hang in the margin
	UChar32 chr;
	while (charIndex < count) {
		U8_NEXT_OR_FFFD((const uint8_t*)chars, charIndex, count, chr);

		if (!u_isWhitespace(chr) && !u_iscntrl(chr)) {
			return iter.preceding(charIndex);
		}
	}

	// Return the break location that's at or before the character we stopped on. Note: if we're on a break, the
	// `U8_FWD_1` will cause `preceding` to back up to it.
	U8_FWD_1(chars, charIndex, count);

	return iter.preceding(charIndex);
}

static void compute_line_visual_runs(LayoutBuildState& state, LayoutInfo& result, SBParagraphRef sbParagraph,
		const char* chars, int32_t count, int32_t lineStart, int32_t lineEnd, int32_t stringOffset,
		size_t& highestRun, int32_t& highestRunCharEnd) {
	SBLineRef sbLine = SBParagraphCreateLine(sbParagraph, lineStart, lineEnd - lineStart);
	auto runCount = SBLineGetRunCount(sbLine);
	auto* sbRuns = SBLineGetRunsPtr(sbLine);
	float maxAscent{};
	float maxDescent{};
	int32_t visualRunLastX{};

	for (int32_t i = 0; i < runCount; ++i) {
		int32_t logicalStart, runLength;
		bool rightToLeft = sbRuns[i].level & 1;
		auto runStart = sbRuns[i].offset - stringOffset;
		auto runEnd = runStart + sbRuns[i].length - 1;

		if (!rightToLeft) {
			auto run = binary_search(0, state.logicalRuns.size(), [&](auto index) {
				return state.logicalRuns[index].charEndIndex <= runStart;
			});
			auto chrIndex = runStart;

			for (;;) {
				auto logicalRunEnd = state.logicalRuns[run].charEndIndex;
				auto fontData = FontRegistry::get_font_data(state.logicalRuns[run].font);

				if (auto ascent = fontData.get_ascent(); ascent > maxAscent) {
					maxAscent = ascent;
				}

				if (auto descent = fontData.get_descent(); descent < maxDescent) {
					maxDescent = descent;
				}

				if (runEnd < logicalRunEnd) {
					append_visual_run(state, result, run, chrIndex + stringOffset, runEnd + stringOffset,
							visualRunLastX, highestRun, highestRunCharEnd);
					break;
				}
				else {
					append_visual_run(state, result, run, chrIndex + stringOffset,
							logicalRunEnd - 1 + stringOffset, visualRunLastX, highestRun, highestRunCharEnd);
					chrIndex = logicalRunEnd;
					++run;
				}
			}
		}
		else {
			auto run = binary_search(0, state.logicalRuns.size(), [&](auto index) {
				return state.logicalRuns[index].charEndIndex <= runEnd;
			});
			auto chrIndex = runEnd;

			for (;;) {
				auto logicalRunStart = run == 0 ? 0 : state.logicalRuns[run - 1].charEndIndex;
				auto fontData = FontRegistry::get_font_data(state.logicalRuns[run].font);

				if (auto ascent = fontData.get_ascent(); ascent > maxAscent) {
					maxAscent = ascent;
				}

				if (auto descent = fontData.get_descent(); descent < maxDescent) {
					maxDescent = descent;
				}

				if (runStart >= logicalRunStart) {
					append_visual_run(state, result, run, runStart + stringOffset, chrIndex + stringOffset,
							visualRunLastX, highestRun, highestRunCharEnd);
					break;
				}
				else {
					append_visual_run(state, result, run, logicalRunStart + stringOffset,
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

static void append_visual_run(LayoutBuildState& state, LayoutInfo& result, size_t run, int32_t charStartIndex,
		int32_t charEndIndex, int32_t& visualRunLastX, size_t& highestRun, int32_t& highestRunCharEnd) {
	auto logicalFirstGlyph = run == 0 ? 0 : state.logicalRuns[run - 1].glyphEndIndex;
	auto logicalLastGlyph = state.logicalRuns[run].glyphEndIndex;
	bool rightToLeft = state.logicalRuns[run].level & 1;
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

	if (rightToLeft) {
		for (uint32_t i = visualLastGlyph; i-- > visualFirstGlyph;) {
			result.append_glyph(state.glyphs[i]);
			result.append_char_index(state.charIndices[i]);

			result.append_glyph_position(scalbnf(visualRunLastX, -6), scalbnf(state.glyphPositions[1][i], -6));
			visualRunLastX += state.glyphPositions[0][i];
		}

		result.append_glyph_position(scalbnf(visualRunLastX, -6),
				scalbnf(state.glyphPositions[1][visualLastGlyph], -6));
	}
	else {
		for (uint32_t i = visualFirstGlyph; i < visualLastGlyph; ++i) {
			result.append_glyph(state.glyphs[i]);
			result.append_char_index(state.charIndices[i]);

			result.append_glyph_position(scalbnf(visualRunLastX, -6), scalbnf(state.glyphPositions[1][i], -6));
			visualRunLastX += state.glyphPositions[0][i];
		}

		result.append_glyph_position(scalbnf(visualRunLastX, -6),
				scalbnf(state.glyphPositions[1][visualLastGlyph], -6));
	}

	result.append_run(state.logicalRuns[run].font, static_cast<uint32_t>(charStartIndex),
			static_cast<uint32_t>(charEndIndex + 1), rightToLeft);
}

// LayoutBuildState

void LayoutBuildState::reset(size_t capacity) {
	glyphs.clear();
	glyphs.reserve(capacity);

	charIndices.clear();
	charIndices.reserve(capacity);

	glyphPositions[0].clear();
	glyphPositions[0].reserve(capacity + 1);
	glyphPositions[1].clear();
	glyphPositions[1].reserve(capacity + 1);

	cursorX = 0;
	cursorY = 0;

	logicalRuns.clear();
}

