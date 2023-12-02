#include "layout_info.hpp"

#include "binary_search.hpp"
#include "value_run_utils.hpp"
#include "font.hpp"
#include "multi_script_font.hpp"

#include <unicode/ubidi.h>
#include <unicode/brkiter.h>
#include <unicode/utf16.h>
#include <usc_impl.h>

#include <hb.h>

#include <cmath>

using namespace Text;

namespace {

struct LayoutBuildState {
	explicit LayoutBuildState()
			: pParaBiDi(ubidi_open())
			, pLineBiDi(ubidi_open())
			, pBuffer(hb_buffer_create()) {
		UErrorCode err{U_ZERO_ERROR};
		pLineBreakIterator = icu::BreakIterator::createLineInstance(icu::Locale::getDefault(), err);
		hb_buffer_set_cluster_level(pBuffer, HB_BUFFER_CLUSTER_LEVEL_MONOTONE_CHARACTERS);
	}

	~LayoutBuildState() {
		ubidi_close(pParaBiDi);
		ubidi_close(pLineBiDi);
		delete pLineBreakIterator;
		hb_buffer_destroy(pBuffer);
	}

	UBiDi* pParaBiDi;
	UBiDi* pLineBiDi;
	icu::BreakIterator* pLineBreakIterator;
	hb_buffer_t* pBuffer;
	std::vector<uint32_t> glyphs;
	std::vector<uint32_t> charIndices;
	std::vector<float> glyphPositions;
	std::vector<int32_t> glyphWidths;
};

struct LogicalRun {
	const Font* pFont;
	const icu::Locale* pLocale;
	UBiDiLevel level;
	UScriptCode script;
	int32_t charEndIndex;
	uint32_t glyphEndIndex;
};

}

static constexpr const UChar32 CH_LF = 0x000A;
static constexpr const UChar32 CH_CR = 0x000D;
static constexpr const UChar32 CH_LSEP = 0x2028;
static constexpr const UChar32 CH_PSEP = 0x2029;

// FIXME: Using `stringOffset` is a bit cumbersome, refactor this logic to have full view of the string
static size_t build_sub_paragraph(LayoutBuildState& state, LayoutInfo& result, const char16_t* chars,
		int32_t count, int32_t stringOffset, const ValueRuns<const MultiScriptFont*>& fontRuns,
		UBiDiLevel paragraphLevel, int32_t fixedWidth);

static ValueRuns<UBiDiLevel> compute_levels(UBiDi* pBiDi, UBiDiLevel paragraphLevel, const char16_t* chars,
		int32_t count);
static ValueRuns<UScriptCode> compute_scripts(const char16_t* chars, int32_t count);
static ValueRuns<const Font*> compute_sub_fonts(const char16_t* chars,
		const ValueRuns<const MultiScriptFont*>& fontRuns, const ValueRuns<UScriptCode>& scriptRuns);

static void shape_logical_run(LayoutBuildState& state, hb_font_t* pFont, const char16_t* chars, int32_t offset,
		int32_t count, int32_t max, UScriptCode script, const icu::Locale& locale, bool rightToLeft,
		int32_t stringOffset);
static int32_t find_previous_line_break(icu::BreakIterator& iter, const char16_t* chars, int32_t count,
		int32_t charIndex);
static void compute_line_visual_runs(LayoutBuildState& state, LayoutInfo& result,
		const std::vector<LogicalRun>& logicalRuns, int32_t lineStart, int32_t lineEnd, int32_t stringOffset,
		size_t& highestRun, int32_t& highestRunCharEnd);
static void append_visual_run(LayoutBuildState& state, LayoutInfo& result, const LogicalRun* logicalRuns,
		size_t logicalRunIndex, int32_t charStartIndex, int32_t charEndIndex, float& visualRunLastX,
		size_t& highestRun, int32_t& highestRunCharEnd);

// Public Functions

void Text::build_layout_info_icu(LayoutInfo& result, const char16_t* chars, int32_t count,
		const ValueRuns<const MultiScriptFont*>& fontRuns, float textAreaWidth, float textAreaHeight,
		TextYAlignment textYAlignment, LayoutInfoFlags flags) {
	result.clear();

	LayoutBuildState state{};

	UText iter UTEXT_INITIALIZER;
	UErrorCode err{};
	utext_openUChars(&iter, chars, count, &err);

	// FIXME: Give the sub-paragraphs a full view of font runs
	ValueRuns<const MultiScriptFont*> subsetFontRuns(fontRuns.get_run_count());
	int32_t byteIndex = 0;
	size_t lastHighestRun = 0;

	UBiDiLevel paragraphLevel = ((flags & LayoutInfoFlags::RIGHT_TO_LEFT) == LayoutInfoFlags::NONE)
			? UBIDI_DEFAULT_LTR : UBIDI_DEFAULT_RTL;

	if ((flags & LayoutInfoFlags::OVERRIDE_DIRECTIONALITY) != LayoutInfoFlags::NONE) {
		paragraphLevel |= UBIDI_LEVEL_OVERRIDE;
	}

	// 26.6 fixed-point text area width
	auto fixedTextAreaWidth = static_cast<int32_t>(textAreaWidth * 64.f);

	for (;;) {
		auto idx = UTEXT_GETNATIVEINDEX(&iter);
		auto c = UTEXT_NEXT32(&iter);

		if (c == U_SENTINEL || c == CH_LF || c == CH_CR || c == CH_LSEP || c == CH_PSEP) {
			if (idx != byteIndex) {
				auto byteCount = idx - byteIndex;

				subsetFontRuns.clear();
				fontRuns.get_runs_subset(byteIndex, byteCount, subsetFontRuns);
				lastHighestRun = build_sub_paragraph(state, result, chars + byteIndex, byteCount, byteIndex,
						subsetFontRuns, paragraphLevel, fixedTextAreaWidth);
			}
			else {
				auto* pFont = fontRuns.get_value(byteIndex == count ? count - 1 : byteIndex);
				auto height = static_cast<float>(pFont->getAscent() + pFont->getDescent());

				lastHighestRun = result.get_run_count();
				result.append_empty_line(static_cast<uint32_t>(byteIndex), height,
						static_cast<float>(pFont->getAscent()));
			}

			if (c == U_SENTINEL) {
				break;
			}
			else if (c == CH_CR && UTEXT_CURRENT32(&iter) == CH_LF) {
				UTEXT_NEXT32(&iter);
			}

			byteIndex = UTEXT_GETNATIVEINDEX(&iter);

			result.set_run_char_end_offset(lastHighestRun, byteIndex - idx);
		}
	}

	auto totalHeight = result.get_text_height();
	result.set_text_start_y(static_cast<float>(textYAlignment) * (textAreaHeight - totalHeight) * 0.5f);
}

// Static Functions

static size_t build_sub_paragraph(LayoutBuildState& state, LayoutInfo& result, const char16_t* chars,
		int32_t count, int32_t stringOffset, const ValueRuns<const MultiScriptFont*>& fontRuns,
		UBiDiLevel paragraphLevel, int32_t textAreaWidth) {
	auto levelRuns = compute_levels(state.pParaBiDi, paragraphLevel, chars, count);
	auto scriptRuns = compute_scripts(chars, count);
	ValueRuns<const icu::Locale*> localeRuns(&icu::Locale::getDefault(), count);
	auto subFontRuns = compute_sub_fonts(chars, fontRuns, scriptRuns);
	int32_t runStart{};

	std::vector<LogicalRun> logicalRuns;

	iterate_run_intersections([&](auto limit, auto* pFont, auto level, auto script, auto* pLocale) {
		logicalRuns.push_back({
			.pFont = pFont,
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
		shape_logical_run(state, run.pFont->get_hb_font(), chars, runStart, run.charEndIndex - runStart,
				count, run.script, *run.pLocale, rightToLeft, stringOffset);
		run.glyphEndIndex = static_cast<uint32_t>(state.glyphs.size());
		runStart = run.charEndIndex;
	}

	size_t highestRun{};
	int32_t highestRunCharEnd{INT32_MIN};

	// If width == 0, perform no line breaking
	if (textAreaWidth == 0) {
		compute_line_visual_runs(state, result, logicalRuns, stringOffset, stringOffset + count,
				stringOffset, highestRun, highestRunCharEnd);
		return highestRun;
	}

	// Find line breaks
	UText uText UTEXT_INITIALIZER;
	UErrorCode err{};
	utext_openUChars(&uText, chars, count, &err);
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
		while (lineEnd <= lineStart) {
			lineEnd = state.charIndices[glyphIndex++];
		}

		compute_line_visual_runs(state, result, logicalRuns, lineStart, lineEnd, stringOffset, highestRun,
				highestRunCharEnd);
	}

	return highestRun;
}

static ValueRuns<UBiDiLevel> compute_levels(UBiDi* pBiDi, UBiDiLevel paragraphLevel, const char16_t* chars,
		int32_t count) {
	UErrorCode err{};
	ubidi_setPara(pBiDi, chars, count, paragraphLevel, nullptr, &err);
	auto levelRunCount = ubidi_countRuns(pBiDi, &err);

	ValueRuns<UBiDiLevel> levelRuns(levelRunCount);

	int32_t logicalStart{};
	int32_t limit;
	UBiDiLevel level;

	for (int32_t run = 0; run < levelRunCount; ++run) {
		ubidi_getLogicalRun(pBiDi, logicalStart, &limit, &level);
		levelRuns.add(limit, level);
		logicalStart = limit;
	}

	return levelRuns;
}

static ValueRuns<UScriptCode> compute_scripts(const char16_t* chars, int32_t count) {
	UErrorCode err{};
	auto* sr = uscript_openRun(chars, count, &err);

	ValueRuns<UScriptCode> scriptRuns;

	int32_t limit;
	UScriptCode script;

	while (uscript_nextRun(sr, nullptr, &limit, &script)) {
		scriptRuns.add(limit, script);
	}

	uscript_closeRun(sr);

	return scriptRuns;
}

static ValueRuns<const Font*> compute_sub_fonts(const char16_t* chars,
		const ValueRuns<const MultiScriptFont*>& fontRuns, const ValueRuns<UScriptCode>& scriptRuns) {
	ValueRuns<const Font*> result(fontRuns.get_run_count());
	int32_t offset{};
	LEErrorCode status{};

	iterate_run_intersections([&](auto limit, auto* pBaseFont, auto script) {
		while (offset < limit) {
			auto* subFont = static_cast<const Font*>(pBaseFont->getSubFont(chars, &offset, limit, script, 
					status));
			result.add(offset, subFont);
		}
	}, fontRuns, scriptRuns);

	return result;
}

static void shape_logical_run(LayoutBuildState& state, hb_font_t* pFont, const char16_t* chars, int32_t offset,
		int32_t count, int32_t max, UScriptCode script, const icu::Locale& locale, bool rightToLeft,
		int32_t stringOffset) {
	hb_buffer_set_script(state.pBuffer, hb_script_from_string(uscript_getShortName(script), 4));
	hb_buffer_set_language(state.pBuffer, hb_language_from_string(locale.getLanguage(), -1));
	hb_buffer_set_direction(state.pBuffer, rightToLeft ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
	hb_buffer_set_length(state.pBuffer, 0);
	hb_buffer_set_flags(state.pBuffer, (hb_buffer_flags_t)((offset == 0 ? HB_BUFFER_FLAG_BOT : 0)
			| (offset + count == max ? HB_BUFFER_FLAG_EOT : 0)));
	hb_buffer_add_utf16(state.pBuffer, (const uint16_t*)chars, max, offset, 0);
	hb_buffer_add_utf16(state.pBuffer, (const uint16_t*)(chars + offset), max - offset, 0, count);

	hb_shape(pFont, state.pBuffer, nullptr, 0);

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

static int32_t find_previous_line_break(icu::BreakIterator& iter, const char16_t* chars, int32_t count,
		int32_t charIndex) {
	// Skip over any whitespace or control characters because they can hang in the margin
	UChar32 chr;
	while (charIndex < count) {
		U16_GET(chars, 0, charIndex, count, chr);

		if (!u_isWhitespace(chr) && !u_iscntrl(chr)) {
			break;
		}

		U16_FWD_1(chars, charIndex, count);
	}

	// Return the break location that's at or before the character we stopped on. Note: if we're on a break, the
	// `+ 1` will cause `preceding` to back up to it.
	return iter.preceding(charIndex + 1);
}

static void compute_line_visual_runs(LayoutBuildState& state, LayoutInfo& result,
		const std::vector<LogicalRun>& logicalRuns, int32_t lineStart, int32_t lineEnd, int32_t stringOffset,
		size_t& highestRun, int32_t& highestRunCharEnd) {
	UErrorCode err{};
	ubidi_setLine(state.pParaBiDi, lineStart - stringOffset, lineEnd - stringOffset, state.pLineBiDi, &err);
	auto runCount = ubidi_countRuns(state.pLineBiDi, &err);
	int32_t maxAscent{};
	int32_t maxDescent{};
	float visualRunLastX{};

	for (int32_t i = 0; i < runCount; ++i) {
		int32_t logicalStart, runLength;
		auto runDir = ubidi_getVisualRun(state.pLineBiDi, i, &logicalStart, &runLength);
		auto runStart = lineStart + logicalStart - stringOffset;
		auto runEnd = runStart + runLength - 1;

		if (runDir == UBIDI_LTR) {
			auto run = binary_search(0, logicalRuns.size(), [&](auto index) {
				return logicalRuns[index].charEndIndex <= runStart;
			});
			auto chrIndex = runStart;

			for (;;) {
				auto logicalRunEnd = logicalRuns[run].charEndIndex;

				if (auto ascent = logicalRuns[run].pFont->getAscent(); ascent > maxAscent) {
					maxAscent = ascent;
				}

				if (auto descent = logicalRuns[run].pFont->getDescent(); descent > maxDescent) {
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

				if (auto ascent = logicalRuns[run].pFont->getAscent(); ascent > maxAscent) {
					maxAscent = ascent;
				}

				if (auto descent = logicalRuns[run].pFont->getDescent(); descent > maxDescent) {
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

	result.append_line(static_cast<float>(maxAscent + maxDescent), static_cast<float>(maxAscent));
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
		for (uint32_t i = visualLastGlyph - 1; ; --i) {
			result.append_glyph(state.glyphs[i]);
			result.append_char_index(state.charIndices[i]);

			if (i == visualFirstGlyph) {
				break;
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

	result.append_run(logicalRuns[run].pFont, static_cast<uint32_t>(charStartIndex),
			static_cast<uint32_t>(charEndIndex + 1), rightToLeft);
}

