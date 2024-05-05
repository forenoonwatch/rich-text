#include "layout_info.hpp"

#include "binary_search.hpp"
#include "value_run_utils.hpp"
#include "font_registry.hpp"
#include "script_run_iterator.hpp"

#include <unicode/brkiter.h>

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

	icu::BreakIterator* pLineBreakIterator;
	hb_buffer_t* pBuffer;
	std::vector<uint32_t> glyphs;
	std::vector<uint32_t> charIndices;
	std::vector<float> glyphPositions;
	std::vector<int32_t> glyphWidths;
};

struct LogicalRun {
	SingleScriptFont font;
	const icu::Locale* pLocale;
	SBLevel level;
	UScriptCode script;
	int32_t charEndIndex;
	uint32_t glyphEndIndex;
};

}

// FIXME: Using `stringOffset` is a bit cumbersome, refactor this logic to have full view of the string
static size_t build_sub_paragraph(LayoutBuildState& state, LayoutInfo& result, SBParagraphRef sbParagraph,
		const char* chars, int32_t count, int32_t stringOffset, const ValueRuns<Font>& fontRuns,
		const ValueRuns<bool>* pSmallcapsRuns, const ValueRuns<bool>* pSubscriptRuns,
		const ValueRuns<bool>* pSuperscriptRuns, int32_t fixedWidth);

static ValueRuns<SBLevel> compute_levels(SBParagraphRef sbParagraph, size_t paragraphLength);
static ValueRuns<UScriptCode> compute_scripts(const char* chars, int32_t count);
static ValueRuns<SingleScriptFont> compute_sub_fonts(const char* chars, const ValueRuns<Font>& fontRuns,
		const ValueRuns<UScriptCode>& scriptRuns);

static void shape_logical_run(LayoutBuildState& state, hb_font_t* pFont, const ValueRuns<bool>* pSmallcapsRuns,
		const ValueRuns<bool>* pSubscriptRuns, const ValueRuns<bool>* pSuperscriptRuns, const char* chars,
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

void Text::build_layout_info_utf8(LayoutInfo& result, const char* chars, int32_t count,
		const ValueRuns<Font>& fontRuns, float textAreaWidth, float textAreaHeight,
		TextYAlignment textYAlignment, LayoutInfoFlags flags, const ValueRuns<bool>* pSmallcapsRuns,
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
			result.append_empty_line(static_cast<uint32_t>(paragraphOffset), height, fontData.get_ascent());
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
		const char* chars, int32_t count, int32_t stringOffset, const ValueRuns<Font>& fontRuns,
		const ValueRuns<bool>* pSmallcapsRuns, const ValueRuns<bool>* pSubscriptRuns,
		const ValueRuns<bool>* pSuperscriptRuns, int32_t textAreaWidth) {
	auto levelRuns = compute_levels(sbParagraph, count);
	auto scriptRuns = compute_scripts(chars, count);
	ValueRuns<const icu::Locale*> localeRuns(&icu::Locale::getDefault(), count);
	auto subFontRuns = compute_sub_fonts(chars, fontRuns, scriptRuns);
	int32_t runStart{};

	std::vector<LogicalRun> logicalRuns;

	iterate_run_intersections([&](auto limit, auto font, auto level, auto script, auto* pLocale) {
		logicalRuns.push_back({
			.font = font,
			.pLocale = pLocale,
			.level = level,
			.script = script,
			.charEndIndex = limit,
		});
	}, subFontRuns, levelRuns, scriptRuns, localeRuns);

	state.glyphs.clear();
	state.glyphs.reserve(count);

	state.charIndices.clear();
	state.charIndices.reserve(count);

	state.glyphPositions.clear();
	state.glyphPositions.reserve(2 * (count + logicalRuns.size()));

	state.glyphWidths.clear();
	state.glyphWidths.reserve(count);

	for (auto& run : logicalRuns) {
		bool rightToLeft = run.level & 1;
		auto fontData = FontRegistry::get_font_data(run.font);
		shape_logical_run(state, fontData.hbFont, pSmallcapsRuns, pSubscriptRuns, pSuperscriptRuns, chars,
				runStart, run.charEndIndex - runStart, count, run.script, *run.pLocale, rightToLeft,
				stringOffset);
		run.glyphEndIndex = static_cast<uint32_t>(state.glyphs.size());
		runStart = run.charEndIndex;
	}

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
		int32_t lineWidthSoFar{};

		lineStart = lineEnd;

		auto glyphIndex = binary_search(0, state.charIndices.size(), [&](auto index) {
			return state.charIndices[index] < lineStart;
		});

		while (glyphIndex < state.glyphs.size()
				&& lineWidthSoFar + state.glyphWidths[glyphIndex] <= textAreaWidth) {
			lineWidthSoFar += state.glyphWidths[glyphIndex];
			++glyphIndex;
		}

		// If no glyphs fit on the line, force one to fit. There shouldn't be any zero width glyphs at the start
		// of a line unless the paragraph consists of only zero width glyphs, because otherwise the zero width
		// glyphs will have been included on the end of the previous line
		if (lineWidthSoFar == 0 && glyphIndex < state.glyphs.size()) {
			++glyphIndex;
		}

		auto charIndex = glyphIndex == state.glyphs.size() ? count + stringOffset
				: state.charIndices[glyphIndex];
		lineEnd = find_previous_line_break(*state.pLineBreakIterator, chars, count, charIndex - stringOffset)
				+ stringOffset;

		// If this break is at or before the last one, find a glyph that produces a break after the last one,
		// starting at the one which didn't fit
		while (lineEnd <= lineStart && glyphIndex < state.glyphs.size()) {
			lineEnd = state.charIndices[glyphIndex++];
		}

		if (lineEnd <= lineStart && glyphIndex == state.glyphs.size()) {
			lineEnd = stringOffset + count;
		}

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
		const ValueRuns<UScriptCode>& scriptRuns) {
	ValueRuns<SingleScriptFont> result(fontRuns.get_run_count());
	int32_t offset{};

	iterate_run_intersections([&](auto limit, auto baseFont, auto script) {
		while (offset < limit) {
			auto subFont = FontRegistry::get_sub_font(baseFont, chars, offset, limit, script);
			result.add(offset, subFont);
		}
	}, fontRuns, scriptRuns);

	return result;
}

static void maybe_add_tag_runs(std::vector<hb_feature_t>& features, const ValueRuns<bool>* pTagRuns,
		hb_tag_t tag, int32_t stringOffset, int32_t count) {
	if (!pTagRuns) {
		return;
	}

	int32_t lastLimit = 0;

	pTagRuns->for_each_run_in_range(stringOffset, count, [&](int32_t limit, bool shouldEmitTag) {
		if (shouldEmitTag) {
			features.push_back({
				.tag = tag,
				.value = 1,
				.start = static_cast<unsigned>(lastLimit),
				.end = static_cast<unsigned>(limit),
			});
		}

		lastLimit = limit;
	});
}

static void shape_logical_run(LayoutBuildState& state, hb_font_t* pFont, const ValueRuns<bool>* pSmallcapsRuns,
		const ValueRuns<bool>* pSubscriptRuns, const ValueRuns<bool>* pSuperscriptRuns, const char* chars,
		int32_t offset, int32_t count, int32_t max, UScriptCode script, const icu::Locale& locale,
		bool rightToLeft, int32_t stringOffset) {
	hb_buffer_set_script(state.pBuffer, hb_script_from_string(uscript_getShortName(script), 4));
	hb_buffer_set_language(state.pBuffer, hb_language_from_string(locale.getLanguage(), -1));
	hb_buffer_set_direction(state.pBuffer, rightToLeft ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
	hb_buffer_set_length(state.pBuffer, 0);
	hb_buffer_set_flags(state.pBuffer, (hb_buffer_flags_t)((offset == 0 ? HB_BUFFER_FLAG_BOT : 0)
			| (offset + count == max ? HB_BUFFER_FLAG_EOT : 0)));
	hb_buffer_add_utf8(state.pBuffer, chars, max, offset, 0);
	hb_buffer_add_utf8(state.pBuffer, chars + offset, max - offset, 0, count);

	std::vector<hb_feature_t> features;
	maybe_add_tag_runs(features, pSmallcapsRuns, HB_TAG('s', 'm', 'c', 'p'), stringOffset, count);
	maybe_add_tag_runs(features, pSubscriptRuns, HB_TAG('s', 'u', 'b', 's'), stringOffset, count);
	maybe_add_tag_runs(features, pSuperscriptRuns, HB_TAG('s', 'u', 'p', 's'), stringOffset, count);

	hb_shape(pFont, state.pBuffer, features.data(), static_cast<unsigned>(features.size()));

	auto glyphCount = hb_buffer_get_length(state.pBuffer);
	auto* glyphPositions = hb_buffer_get_glyph_positions(state.pBuffer, nullptr);
	auto* glyphInfos = hb_buffer_get_glyph_infos(state.pBuffer, nullptr);
	int32_t cursorX{};
	int32_t cursorY{};

	for (unsigned i = 0; i < glyphCount; ++i) {
		state.glyphPositions.emplace_back(scalbnf(cursorX + glyphPositions[i].x_offset, -6));
		state.glyphPositions.emplace_back(scalbnf(cursorY + glyphPositions[i].y_offset, -6));
		cursorX += glyphPositions[i].x_advance;
		cursorY += glyphPositions[i].y_advance;
	}

	state.glyphPositions.emplace_back(scalbnf(cursorX, -6));
	state.glyphPositions.emplace_back(scalbnf(cursorY, -6));

	if (rightToLeft) {
		for (unsigned i = glyphCount - 1; ; --i) {
			state.glyphs.emplace_back(glyphInfos[i].codepoint);
			state.charIndices.emplace_back(glyphInfos[i].cluster + offset + stringOffset);

			if (i == glyphCount - 1) {
				state.glyphWidths.emplace_back(glyphPositions[i].x_advance - glyphPositions[i].x_offset);
			}
			else {
				state.glyphWidths.emplace_back(glyphPositions[i].x_advance + glyphPositions[i + 1].x_offset
						- glyphPositions[i].x_offset);
			}

			if (i == 0) {
				break;
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
	auto logicalFirstPos = run == 0 ? 0 : 2 * (logicalRuns[run - 1].glyphEndIndex + run);
	bool rightToLeft = logicalRuns[run].level & 1;
	uint32_t visualFirstGlyph;
	uint32_t visualLastGlyph;

	if (charEndIndex > highestRunCharEnd) {
		highestRun = result.get_run_count();
		highestRunCharEnd = charEndIndex;
	}

	visualFirstGlyph = binary_search(logicalFirstGlyph,
			logicalLastGlyph - logicalFirstGlyph, [&](auto index) {
		return state.charIndices[index] < charStartIndex;
	});

	visualLastGlyph = binary_search(visualFirstGlyph,
			logicalLastGlyph - visualFirstGlyph, [&](auto index) {
		return state.charIndices[index] <= charEndIndex;
	});

	uint32_t visualFirstPosIndex;
	uint32_t visualLastPosIndex;

	if (rightToLeft) {
		if (visualLastGlyph > visualFirstGlyph) {
			for (uint32_t i = visualLastGlyph - 1; ; --i) {
				result.append_glyph(state.glyphs[i]);
				result.append_char_index(state.charIndices[i]);

				if (i == visualFirstGlyph) {
					break;
				}
			}
		}

		visualFirstPosIndex = logicalFirstGlyph + (logicalLastGlyph - visualLastGlyph);
		visualLastPosIndex = logicalLastGlyph - (visualFirstGlyph - logicalFirstGlyph);
	}
	else {
		for (uint32_t i = visualFirstGlyph; i < visualLastGlyph; ++i) {
			result.append_glyph(state.glyphs[i]);
			result.append_char_index(state.charIndices[i]);
		}

		visualFirstPosIndex = visualFirstGlyph;
		visualLastPosIndex = visualLastGlyph;
	}

	visualRunLastX -= state.glyphPositions[logicalFirstPos + 2 * (visualFirstPosIndex - logicalFirstGlyph)];

	for (uint32_t i = visualFirstPosIndex; i < visualLastPosIndex; ++i) {
		auto posIndex = logicalFirstPos + 2 * (i - logicalFirstGlyph);
		result.append_glyph_position(state.glyphPositions[posIndex] + visualRunLastX,
				state.glyphPositions[posIndex + 1]);
	}

	auto logicalLastPos = logicalFirstPos + 2 * (visualLastPosIndex - logicalFirstGlyph);
	result.append_glyph_position(state.glyphPositions[logicalLastPos] + visualRunLastX,
			state.glyphPositions[logicalLastPos + 1]);

	visualRunLastX += state.glyphPositions[logicalLastPos];

	result.append_run(logicalRuns[run].font, static_cast<uint32_t>(charStartIndex),
			static_cast<uint32_t>(charEndIndex + 1), rightToLeft);
}

