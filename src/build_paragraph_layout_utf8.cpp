#include "paragraph_layout.hpp"

#include "text_run_utils.hpp"
#include "font.hpp"
#include "multi_script_font.hpp"
#include "utf_conversion_util.hpp"

#include <unicode/ubidi.h>
#include <unicode/brkiter.h>
#include <usc_impl.h>

#include <hb.h>

#include <cmath>

using namespace RichText;

namespace {

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

static void layout_build_state_init(LayoutBuildState& state);

// FIXME: Using `stringOffset` is a bit cumbersome, refactor this logic to have full view of the string
static void build_sub_paragraph(LayoutBuildState& state, ParagraphLayout& result,
		std::vector<uint32_t>& lineFirstCharIndices, const char* chars, int32_t count, int32_t stringOffset,
		const TextRuns<const MultiScriptFont*>& fontRuns, UBiDiLevel paragraphLevel, int32_t fixedWidth);

static TextRuns<UBiDiLevel> compute_levels(UBiDi* pBiDi, UBiDiLevel paragraphLevel,
		const icu::UnicodeString& uniStr, const char* chars, int32_t count);
static TextRuns<UScriptCode> compute_scripts(const icu::UnicodeString& uniStr, const char* chars, int32_t count);
static TextRuns<const Font*> compute_sub_fonts(const char* chars,
		const TextRuns<const MultiScriptFont*>& fontRuns, const TextRuns<UScriptCode>& scriptRuns);

static void shape_logical_run(ParagraphLayout& result, std::vector<float>& positions,
		std::vector<int32_t>& widths, hb_buffer_t* pBuffer, hb_font_t* pFont, const char* chars,
		int32_t offset, int32_t count, int32_t max, UScriptCode script, const icu::Locale& locale,
		bool rightToLeft, int32_t stringOffset);
static int32_t find_previous_line_break(icu::BreakIterator& iter, const char* chars, int32_t count,
		int32_t charIndex);
static void compute_line_visual_runs(LayoutBuildState& state, ParagraphLayout& result,
		const std::vector<LogicalRun>& logicalRuns, const float* positions, const icu::UnicodeString& uniStr,
		const char* chars, int32_t count, int32_t lineStart, int32_t lineEnd, size_t& visualRunEndIndex,
		int32_t stringOffset, uint32_t firstGlyphIndex);
static void append_visual_run(ParagraphLayout& result, const LogicalRun* logicalRuns, const float* positions,
		size_t logicalRunIndex, int32_t charEndIndex, size_t& visualRunEndIndex, float& visualRunLastX,
		uint32_t paragraphFirstGlyphIndex);

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

void build_paragraph_layout_utf8(LayoutBuildState& state, ParagraphLayout& result, const char* chars,
		int32_t count, const TextRuns<const MultiScriptFont*>& fontRuns, float textAreaWidth,
		float textAreaHeight, TextYAlignment textYAlignment, ParagraphLayoutFlags flags) {
	layout_build_state_init(state);
	UText iter UTEXT_INITIALIZER;
	UErrorCode err{};
	utext_openUTF8(&iter, chars, count, &err);

	// FIXME: Give the sub-paragraphs a full view of font runs
	TextRuns<const MultiScriptFont*> subsetFontRuns(fontRuns.get_value_count());
	// FIXME: Can this potentially be removed?
	std::vector<uint32_t> lineFirstCharIndices;
	int32_t byteIndex = 0;

	UBiDiLevel paragraphLevel = ((flags & ParagraphLayoutFlags::RIGHT_TO_LEFT) == ParagraphLayoutFlags::NONE)
			? UBIDI_DEFAULT_LTR : UBIDI_DEFAULT_RTL;

	if ((flags & ParagraphLayoutFlags::OVERRIDE_DIRECTIONALITY) != ParagraphLayoutFlags::NONE) {
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
				build_sub_paragraph(state, result, lineFirstCharIndices, chars + byteIndex, byteCount,
						byteIndex, subsetFontRuns, paragraphLevel, fixedTextAreaWidth);
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

// Static Functions

static void layout_build_state_init(LayoutBuildState& state) {
	if (!state.pParaBiDi) {
		state.pParaBiDi = ubidi_open();
	}

	if (!state.pLineBiDi) {
		state.pLineBiDi = ubidi_open();
	}

	if (!state.pLineBreakIterator) {
		UErrorCode err{U_ZERO_ERROR};
		state.pLineBreakIterator = icu::BreakIterator::createLineInstance(icu::Locale::getDefault(), err);
	}

	if (!state.pBuffer) {
		state.pBuffer = hb_buffer_create();
		hb_buffer_set_cluster_level(state.pBuffer, HB_BUFFER_CLUSTER_LEVEL_MONOTONE_CHARACTERS);
	}
}

static void build_sub_paragraph(LayoutBuildState& state, ParagraphLayout& result,
		std::vector<uint32_t>& lineFirstCharIndices, const char* chars, int32_t count, int32_t stringOffset,
		const TextRuns<const MultiScriptFont*>& fontRuns, UBiDiLevel paragraphLevel, int32_t textAreaWidth) {
	auto uniStr = icu::UnicodeString::fromUTF8({chars, count});
	auto levelRuns = compute_levels(state.pParaBiDi, paragraphLevel, uniStr, chars, count);
	auto scriptRuns = compute_scripts(uniStr, chars, count);
	TextRuns<const icu::Locale*> localeRuns(&icu::Locale::getDefault(), count);
	auto subFontRuns = compute_sub_fonts(chars, fontRuns, scriptRuns);
	int32_t runStart{};

	std::vector<LogicalRun> logicalRuns;
	std::vector<float> logicalPositions;
	std::vector<int32_t> logicalWidths;

	iterate_run_intersections([&](auto limit, auto* pFont, auto level, auto script, auto* pLocale) {
		logicalRuns.push_back({
			.pFont = pFont,
			.pLocale = pLocale,
			.level = level,
			.script = script,
			.charEndIndex = limit,
		});
	}, subFontRuns, levelRuns, scriptRuns, localeRuns);

	logicalPositions.reserve(2 * (count + logicalRuns.size()));
	logicalWidths.reserve(count);

	auto firstGlyphIndex = static_cast<uint32_t>(result.glyphs.size());

	for (auto& run : logicalRuns) {
		bool rightToLeft = run.level & 1;
		shape_logical_run(result, logicalPositions, logicalWidths, state.pBuffer, run.pFont->get_hb_font(),
				chars, runStart, run.charEndIndex - runStart, count, run.script, *run.pLocale, rightToLeft,
				stringOffset);
		run.glyphEndIndex = static_cast<uint32_t>(result.glyphs.size());
		runStart = run.charEndIndex;
	}

	size_t visualRunGlyphEndIndex = firstGlyphIndex;

	// If width == 0, perform no line breaking
	if (textAreaWidth == 0) {
		compute_line_visual_runs(state, result, logicalRuns, logicalPositions.data(), uniStr, chars, count,
				stringOffset, stringOffset + count, visualRunGlyphEndIndex, stringOffset, firstGlyphIndex);
		lineFirstCharIndices.emplace_back(static_cast<uint32_t>(stringOffset));
		return;
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

		auto glyphIndex = binary_search(firstGlyphIndex, result.charIndices.size() - firstGlyphIndex,
				[&](auto index) {
			return result.charIndices[index] < lineStart;
		});

		while (glyphIndex < result.glyphs.size()
				&& lineWidthSoFar + logicalWidths[glyphIndex - firstGlyphIndex] <= textAreaWidth) {
			lineWidthSoFar += logicalWidths[glyphIndex - firstGlyphIndex];
			++glyphIndex;
		}

		// If no glyphs fit on the line, force one to fit. There shouldn't be any zero width glyphs at the start
		// of a line unless the paragraph consists of only zero width glyphs, because otherwise the zero width
		// glyphs will have been included on the end of the previous line
		if (lineWidthSoFar == 0 && glyphIndex < result.glyphs.size()) {
			++glyphIndex;
		}

		auto charIndex = glyphIndex == result.glyphs.size() ? count + stringOffset
				: result.charIndices[glyphIndex];
		lineEnd = find_previous_line_break(*state.pLineBreakIterator, chars, count, charIndex - stringOffset)
				+ stringOffset;

		// If this break is at or before the last one, find a glyph that produces a break after the last one,
		// starting at the one which didn't fit
		while (lineEnd <= lineStart) {
			lineEnd = result.charIndices[glyphIndex++];
		}

		compute_line_visual_runs(state, result, logicalRuns, logicalPositions.data(), uniStr, chars, count,
				lineStart, lineEnd, visualRunGlyphEndIndex, stringOffset, firstGlyphIndex);

		lineFirstCharIndices.emplace_back(static_cast<uint32_t>(lineStart));
	}
}

static TextRuns<UBiDiLevel> compute_levels(UBiDi* pBiDi, UBiDiLevel paragraphLevel,
		const icu::UnicodeString& uniStr, const char* chars, int32_t count) {
	UErrorCode err{};
	ubidi_setPara(pBiDi, uniStr.getBuffer(), uniStr.length(), paragraphLevel, nullptr, &err);
	auto levelRunCount = ubidi_countRuns(pBiDi, &err);

	TextRuns<UBiDiLevel> levelRuns(levelRunCount);

	int32_t logicalStart{};
	int32_t limit;
	UBiDiLevel level;
	uint32_t charIndex8{};
	uint32_t charIndex16{};

	for (int32_t run = 0; run < levelRunCount; ++run) {
		ubidi_getLogicalRun(pBiDi, logicalStart, &limit, &level);
		auto limit8 = utf16_index_to_utf8(uniStr.getBuffer(), uniStr.length(), chars, count, limit,
				charIndex16, charIndex8);
		levelRuns.add(limit8, level);
		logicalStart = limit;
	}

	return levelRuns;
}

static TextRuns<UScriptCode> compute_scripts(const icu::UnicodeString& uniStr, const char* chars,
		int32_t count) {
	UErrorCode err{};
	auto* sr = uscript_openRun(uniStr.getBuffer(), uniStr.length(), &err);

	TextRuns<UScriptCode> scriptRuns;

	int32_t limit;
	UScriptCode script;
	uint32_t charIndex8{};
	uint32_t charIndex16{};

	while (uscript_nextRun(sr, nullptr, &limit, &script)) {
		scriptRuns.add(utf16_index_to_utf8(uniStr.getBuffer(), uniStr.length(), chars, count, limit,
				charIndex16, charIndex8), script);
	}

	uscript_closeRun(sr);

	return scriptRuns;
}

static TextRuns<const Font*> compute_sub_fonts(const char* chars,
		const TextRuns<const MultiScriptFont*>& fontRuns, const TextRuns<UScriptCode>& scriptRuns) {
	TextRuns<const Font*> result(fontRuns.get_value_count());
	int32_t offset{};

	iterate_run_intersections([&](auto limit, auto* pBaseFont, auto script) {
		while (offset < limit) {
			auto* subFont = pBaseFont->get_sub_font(chars, offset, limit, script);
			result.add(offset, subFont);
		}
	}, fontRuns, scriptRuns);

	return result;
}

static void shape_logical_run(ParagraphLayout& result, std::vector<float>& positions,
		std::vector<int32_t>& widths, hb_buffer_t* pBuffer, hb_font_t* pFont, const char* chars,
		int32_t offset, int32_t count, int32_t max, UScriptCode script, const icu::Locale& locale,
		bool rightToLeft, int32_t stringOffset) {
	hb_buffer_set_script(pBuffer, hb_script_from_string(uscript_getShortName(script), 4));
	hb_buffer_set_language(pBuffer, hb_language_from_string(locale.getLanguage(), -1));
	hb_buffer_set_direction(pBuffer, rightToLeft ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
	hb_buffer_set_length(pBuffer, 0);
	hb_buffer_set_flags(pBuffer, (hb_buffer_flags_t)((offset == 0 ? HB_BUFFER_FLAG_BOT : 0)
			| (offset + count == max ? HB_BUFFER_FLAG_EOT : 0)));
	hb_buffer_add_utf8(pBuffer, chars, max, offset, 0);
	hb_buffer_add_utf8(pBuffer, chars + offset, max - offset, 0, count);

	hb_shape(pFont, pBuffer, nullptr, 0);

	auto glyphCount = hb_buffer_get_length(pBuffer);
	auto* glyphPositions = hb_buffer_get_glyph_positions(pBuffer, nullptr);
	auto* glyphInfos = hb_buffer_get_glyph_infos(pBuffer, nullptr);
	int32_t cursorX{};
	int32_t cursorY{};

	for (unsigned i = 0; i < glyphCount; ++i) {
		positions.emplace_back(scalbnf(cursorX + glyphPositions[i].x_offset, -6));
		positions.emplace_back(scalbnf(cursorY + glyphPositions[i].y_offset, -6));
		cursorX += glyphPositions[i].x_advance;
		cursorY += glyphPositions[i].y_advance;
	}

	positions.emplace_back(scalbnf(cursorX, -6));
	positions.emplace_back(scalbnf(cursorY, -6));

	if (rightToLeft) {
		for (unsigned i = glyphCount - 1; ; --i) {
			result.glyphs.emplace_back(glyphInfos[i].codepoint);
			result.charIndices.emplace_back(glyphInfos[i].cluster + offset + stringOffset);

			if (i == glyphCount - 1) {
				widths.emplace_back(glyphPositions[i].x_advance - glyphPositions[i].x_offset);
			}
			else {
				widths.emplace_back(glyphPositions[i].x_advance + glyphPositions[i + 1].x_offset
						- glyphPositions[i].x_offset);
			}

			if (i == 0) {
				break;
			}
		}
	}
	else {
		for (unsigned i = 0; i < glyphCount; ++i) {
			result.glyphs.emplace_back(glyphInfos[i].codepoint);
			result.charIndices.emplace_back(glyphInfos[i].cluster + offset + stringOffset);

			if (i == glyphCount - 1) {
				widths.emplace_back(glyphPositions[i].x_advance - glyphPositions[i].x_offset);
			}
			else {
				widths.emplace_back(glyphPositions[i].x_advance + glyphPositions[i + 1].x_offset
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

static void compute_line_visual_runs(LayoutBuildState& state, ParagraphLayout& result,
		const std::vector<LogicalRun>& logicalRuns, const float* positions, const icu::UnicodeString& uniStr,
		const char* chars, int32_t count, int32_t lineStart, int32_t lineEnd, size_t& visualRunEndIndex,
		int32_t stringOffset, uint32_t firstGlyphIndex) {
	uint32_t charIndex8{};
	uint32_t charIndex16{};
	auto bidiLineBegin = utf8_index_to_utf16(chars, count, uniStr.getBuffer(), uniStr.length(),
			lineStart - stringOffset, charIndex8, charIndex16);
	auto bidiLineEnd = utf8_index_to_utf16(chars, count, uniStr.getBuffer(), uniStr.length(),
			lineEnd - stringOffset, charIndex8, charIndex16);
	UErrorCode err{};
	ubidi_setLine(state.pParaBiDi, bidiLineBegin, bidiLineEnd, state.pLineBiDi, &err);
	auto runCount = ubidi_countRuns(state.pLineBiDi, &err);
	int32_t maxAscent{};
	int32_t maxDescent{};
	float visualRunLastX{};

	for (int32_t i = 0; i < runCount; ++i) {
		int32_t logicalStart, runLength;
		auto runDir = ubidi_getVisualRun(state.pLineBiDi, i, &logicalStart, &runLength);
		auto runStart = utf16_index_to_utf8(uniStr.getBuffer(), uniStr.length(), chars, count,
				bidiLineBegin + logicalStart);
		auto runEnd = utf16_index_to_utf8(uniStr.getBuffer(), uniStr.length(), chars, count,
				bidiLineBegin + logicalStart + runLength - 1);
		// FIXME: reconcile utf8
		//auto runStart = lineStart + logicalStart - stringOffset;
		//auto runEnd = runStart + runLength - 1;

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
					append_visual_run(result, logicalRuns.data(), positions, run, runEnd + stringOffset,
							visualRunEndIndex, visualRunLastX, firstGlyphIndex);
					break;
				}
				else {
					append_visual_run(result, logicalRuns.data(), positions, run, logicalRunEnd + stringOffset,
							visualRunEndIndex, visualRunLastX, firstGlyphIndex);
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
					append_visual_run(result, logicalRuns.data(), positions, run, chrIndex + stringOffset,
							visualRunEndIndex, visualRunLastX, firstGlyphIndex);
					break;
				}
				else {
					append_visual_run(result, logicalRuns.data(), positions, run, chrIndex + stringOffset,
							visualRunEndIndex, visualRunLastX, firstGlyphIndex);
					chrIndex = logicalRunStart;
					--run;
				}
			}
		}
	}

	auto height = static_cast<float>(maxAscent + maxDescent);

	auto lastRunIndex = static_cast<uint32_t>(result.visualRuns.size()) - 1;
	auto width = result.visualRuns[lastRunIndex].rightToLeft
			? result.glyphPositions[result.get_first_position_index(lastRunIndex)]
			: result.glyphPositions[2 * (result.visualRuns[lastRunIndex].glyphEndIndex + lastRunIndex)];

	result.lines.push_back({
		.visualRunsEndIndex = static_cast<uint32_t>(result.visualRuns.size()),
		.width = width,
		.ascent = static_cast<float>(maxAscent),
		.totalDescent = result.lines.empty() ? height : result.lines.back().totalDescent + height,
	});
}

static void append_visual_run(ParagraphLayout& result, const LogicalRun* logicalRuns, const float* positions,
		size_t run, int32_t charEndIndex, size_t& visualRunEndIndex, float& visualRunLastX,
		uint32_t paragraphFirstGlyphIndex) {
	auto logicalFirstGlyph = run == 0 ? paragraphFirstGlyphIndex : logicalRuns[run - 1].glyphEndIndex;
	auto logicalFirstPos = run == 0 ? 0
			: 2 * (logicalRuns[run - 1].glyphEndIndex - paragraphFirstGlyphIndex + run);
	auto glyphStartIndex = visualRunEndIndex;
	bool rightToLeft = logicalRuns[run].level & 1;

	visualRunEndIndex = binary_search(visualRunEndIndex, result.charIndices.size() - visualRunEndIndex,
			[&](auto index) {
		return result.charIndices[index] <= charEndIndex;
	});

	if (rightToLeft) {
		auto logicalGlyphEnd = logicalRuns[run].glyphEndIndex;
		auto posBias = positions[logicalFirstPos + 2 * (logicalGlyphEnd - visualRunEndIndex)];
		auto posEnd = positions[logicalFirstPos + 2 * (logicalGlyphEnd - glyphStartIndex)];
		visualRunLastX -= posBias;

		for (auto glyphIndex = logicalGlyphEnd - glyphStartIndex; ; --glyphIndex) {
			auto posIndex = logicalFirstPos + 2 * glyphIndex;
			result.glyphPositions.emplace_back(positions[posIndex] + visualRunLastX);
			result.glyphPositions.emplace_back(positions[posIndex + 1]);

			if (glyphIndex == logicalGlyphEnd - visualRunEndIndex) {
				break;
			}
		}

		visualRunLastX += posEnd;
	}
	else {
		visualRunLastX -= positions[logicalFirstPos + 2 * (glyphStartIndex - logicalFirstGlyph)];

		for (auto glyphIndex = glyphStartIndex; glyphIndex <= visualRunEndIndex; ++glyphIndex) {
			auto posIndex = logicalFirstPos + 2 * (glyphIndex - logicalFirstGlyph);
			result.glyphPositions.emplace_back(positions[posIndex] + visualRunLastX);
			result.glyphPositions.emplace_back(positions[posIndex + 1]);
		}

		visualRunLastX += positions[logicalFirstPos + 2 * (visualRunEndIndex - logicalFirstGlyph)];
	}

	result.visualRuns.push_back({
		.pFont = logicalRuns[run].pFont,
		.glyphEndIndex = static_cast<uint32_t>(visualRunEndIndex),
		.rightToLeft = rightToLeft,
	});
}

