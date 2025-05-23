#include "layout_builder.hpp"

#include "binary_search.hpp"
#include "font_registry.hpp"
#include "layout_info.hpp"
#include "script_run_iterator.hpp"
#include "value_runs.hpp"
#include "value_run_utils.hpp"

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

static int32_t find_previous_line_break(icu::BreakIterator& iter, const char* chars, int32_t count,
		int32_t charIndex);

static void maybe_add_tag_runs(std::vector<hb_feature_t>& features, hb_tag_t tag, int32_t count,
		bool needsFeature, bool isSynthesizingThis);
static void remap_char_indices(hb_glyph_info_t* glyphInfos, unsigned glyphCount, icu::Edits& edits,
		const char* sourceStr, bool rightToLeft);

static constexpr int32_t mul_fixed(int32_t a, int32_t b) {
	auto ab = static_cast<int64_t>(a) * static_cast<int64_t>(b);
	return static_cast<int32_t>(ab >> 6);
}

LayoutBuilder::LayoutBuilder()
		: m_buffer(hb_buffer_create()) {
	UErrorCode err{U_ZERO_ERROR};
	m_lineBreakIterator = icu::BreakIterator::createLineInstance(icu::Locale::getDefault(), err);
	hb_buffer_set_cluster_level(m_buffer, HB_BUFFER_CLUSTER_LEVEL_MONOTONE_CHARACTERS);
}

LayoutBuilder::~LayoutBuilder() {
	if (m_lineBreakIterator) {
		delete m_lineBreakIterator;
	}

	if (m_buffer) {
		hb_buffer_destroy(m_buffer);
	}
}

LayoutBuilder::LayoutBuilder(LayoutBuilder&& other) noexcept {
	*this = std::move(other);
}

LayoutBuilder& LayoutBuilder::operator=(LayoutBuilder&& other) noexcept {
	std::swap(m_lineBreakIterator, other.m_lineBreakIterator);
	std::swap(m_buffer, other.m_buffer);
	m_glyphs = std::move(other.m_glyphs);
	m_charIndices = std::move(other.m_charIndices);
	m_glyphPositions[0] = std::move(other.m_glyphPositions[0]);
	m_glyphPositions[1] = std::move(other.m_glyphPositions[1]);
	std::swap(m_cursor, other.m_cursor);
	m_logicalRuns = std::move(other.m_logicalRuns);

	return *this;
}

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
 */
void LayoutBuilder::build_layout_info(LayoutInfo& result, const char* chars, int32_t count,
		const ValueRuns<Font>& fontRuns, const LayoutBuildParams& params) {
	result.clear();

	SBCodepointSequence codepointSequence{SBStringEncodingUTF8, (void*)chars, (size_t)count};
	SBAlgorithmRef sbAlgorithm = SBAlgorithmCreate(&codepointSequence);
	size_t paragraphOffset{};	

	ValueRunsIterator itFont(fontRuns);
	MaybeDefaultRunsIterator itSmallcaps(params.pSmallcapsRuns, false, count);
	MaybeDefaultRunsIterator itSubscript(params.pSubscriptRuns, false, count);
	MaybeDefaultRunsIterator itSuperscript(params.pSuperscriptRuns, false, count);

	size_t lastHighestRun = 0;

	SBLevel baseDefaultLevel = SBLevelDefaultLTR;

	if ((params.flags & LayoutInfoFlags::OVERRIDE_DIRECTIONALITY) != LayoutInfoFlags::NONE) {
		baseDefaultLevel = static_cast<SBLevel>(params.flags & LayoutInfoFlags::RIGHT_TO_LEFT);
	}
	else {
		baseDefaultLevel = ((params.flags & LayoutInfoFlags::RIGHT_TO_LEFT) == LayoutInfoFlags::NONE)
				? SBLevelDefaultLTR : SBLevelDefaultRTL;
	}

	// 26.6 fixed-point metrics
	auto fixedTextAreaWidth = static_cast<int32_t>(params.textAreaWidth * 64.f);
	auto tabWidthFixed = static_cast<int32_t>(params.tabWidth * 64.f);

	bool usePixelTabWidth = (params.flags & LayoutInfoFlags::TAB_WIDTH_PIXELS) != LayoutInfoFlags::NONE;
	bool vertical = (params.flags & LayoutInfoFlags::VERTICAL) != LayoutInfoFlags::NONE;

	auto& locale = icu::Locale::getDefault();

	while (paragraphOffset < count) {
		size_t paragraphLength, separatorLength;
		SBAlgorithmGetParagraphBoundary(sbAlgorithm, paragraphOffset, INT32_MAX, &paragraphLength,
				&separatorLength);
		bool isLastParagraph = paragraphOffset + paragraphLength == count;

		if (paragraphLength - separatorLength > 0) {
			auto byteCount = paragraphLength - separatorLength;
			
			SBParagraphRef sbParagraph = SBAlgorithmCreateParagraph(sbAlgorithm, paragraphOffset,
					paragraphLength, baseDefaultLevel);
			lastHighestRun = build_paragraph(result, sbParagraph, chars, byteCount, paragraphOffset, itFont,
					itSmallcaps, itSubscript, itSuperscript, fixedTextAreaWidth, tabWidthFixed, locale,
					usePixelTabWidth, vertical);
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

		result.set_run_char_end_offset(lastHighestRun, separatorLength);

		// Append empty line if string ends with a line break
		if (isLastParagraph && separatorLength > 0) {
			auto font = fontRuns.get_value(paragraphOffset == count ? count - 1 : paragraphOffset);
			auto fontData = FontRegistry::get_font_data(font);
			auto height = fontData.get_ascent() - fontData.get_descent();

			result.append_empty_line(FontRegistry::get_default_single_script_font(font),
					static_cast<uint32_t>(paragraphOffset + paragraphLength), height,
					fontData.get_ascent());
			result.set_run_char_end_offset(result.get_run_count() - 1, 0);
		}

		paragraphOffset += paragraphLength;
	}

	auto totalHeight = result.get_text_height();
	result.set_text_start_y(static_cast<float>(params.yAlignment)
			* (params.textAreaHeight - totalHeight) * 0.5f);

	SBAlgorithmRelease(sbAlgorithm);
}

size_t LayoutBuilder::build_paragraph(LayoutInfo& result, SBParagraphRef sbParagraph, const char* fullText,
		int32_t paragraphLength, int32_t paragraphStart, ValueRunsIterator<Font>& itFont,
		MaybeDefaultRunsIterator<bool>& itSmallcaps, MaybeDefaultRunsIterator<bool>& itSubscript,
		MaybeDefaultRunsIterator<bool>& itSuperscript, int32_t textAreaWidth, int32_t tabWidthFixed,
		const icu::Locale& defaultLocale, bool tabWidthFromPixels, bool vertical) {
	const char* paragraphText = fullText + paragraphStart;
	auto paragraphEnd = paragraphStart + paragraphLength;
	auto primaryAxis = static_cast<size_t>(vertical);
	auto secondaryAxis = static_cast<size_t>(!vertical);

	reset(paragraphLength);

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

			shape_logical_run(subFont, paragraphText, runStart - paragraphStart, subFontOffset - runStart,
					paragraphStart, paragraphLength, script, defaultLocale, level & 1, vertical);

			if (!m_logicalRuns.empty() && m_logicalRuns.back().font == subFont) {
				m_logicalRuns.back().charEndIndex = subFontOffset;
				m_logicalRuns.back().glyphEndIndex = static_cast<uint32_t>(m_glyphs.size());
			}
			else {
				m_logicalRuns.push_back({
					.font = subFont,
					.charEndIndex = subFontOffset,
					.glyphEndIndex = static_cast<uint32_t>(m_glyphs.size()),
				});
			}
		}
	}, itFont, itScripts, itLevels, itSmallcaps, itSubscript, itSuperscript);

	// Finalize the last advance after the last character in the paragraph
	m_glyphPositions[secondaryAxis].emplace_back(m_cursor);

	size_t highestRun{};
	int32_t highestRunCharEnd{INT32_MIN};

	// If width == 0, perform no line breaking
	if (textAreaWidth == 0) {
		apply_tab_widths_no_line_break(fullText, tabWidthFixed, tabWidthFromPixels,
				m_glyphPositions[primaryAxis].data());
		compute_line_visual_runs(result, sbParagraph, paragraphText, paragraphLength, paragraphStart,
				paragraphEnd, highestRun, highestRunCharEnd, vertical);
		return highestRun;
	}

	// Find line breaks
	UText uText UTEXT_INITIALIZER;
	UErrorCode err{};
	utext_openUTF8(&uText, paragraphText, paragraphLength, &err);
	m_lineBreakIterator->setText(&uText, err);

	int32_t lineEnd = paragraphStart;
	int32_t lineStart;

	auto& glyphWidths = m_glyphPositions[primaryAxis];

	while (lineEnd < paragraphStart + paragraphLength) {
		int32_t lineWidthSoFar{};

		lineStart = lineEnd;

		auto glyphIndex = binary_search(0, m_charIndices.size(), [&](auto index) {
			return m_charIndices[index] < lineStart;
		});

		while (glyphIndex < m_glyphs.size()) {
			if (fullText[m_charIndices[glyphIndex]] == '\t') {
				auto baseTabWidth = tabWidthFromPixels ? tabWidthFixed
						: mul_fixed(glyphWidths[glyphIndex], tabWidthFixed);
				glyphWidths[glyphIndex] = baseTabWidth - (lineWidthSoFar % baseTabWidth);
			}

			if (lineWidthSoFar + glyphWidths[glyphIndex] > textAreaWidth) {
				break;
			}

			lineWidthSoFar += glyphWidths[glyphIndex];
			++glyphIndex;
		}

		auto glyphIndexBefore = glyphIndex;

		// If no glyphs fit on the line, force one to fit. There shouldn't be any zero width glyphs at the start
		// of a line unless the paragraph consists of only zero width glyphs, because otherwise the zero width
		// glyphs will have been included on the end of the previous line
		if (lineWidthSoFar == 0 && glyphIndex < m_glyphs.size()) {
			++glyphIndex;
		}

		auto charIndex = glyphIndex == m_glyphs.size() ? paragraphLength + paragraphStart
				: m_charIndices[glyphIndex];
		lineEnd = find_previous_line_break(*m_lineBreakIterator, paragraphText, paragraphLength,
				charIndex - paragraphStart) + paragraphStart;

		// If this break is at or before the last one, find a glyph that produces a break after the last one,
		// starting at the one which didn't fit
		while (lineEnd <= lineStart && glyphIndex < m_glyphs.size()) {
			lineEnd = m_charIndices[glyphIndex++];
		}

		if (lineEnd <= lineStart && glyphIndex == m_glyphs.size()) {
			lineEnd = paragraphStart + paragraphLength;
		}

		// Adjust tab widths for glyphs included in the line after the line width calculation before
		for (; glyphIndexBefore < glyphIndex; ++glyphIndexBefore) {
			if (fullText[m_charIndices[glyphIndexBefore]] == '\t') {
				auto baseTabWidth = tabWidthFromPixels ? tabWidthFixed
						: mul_fixed(glyphWidths[glyphIndexBefore], tabWidthFixed);
				glyphWidths[glyphIndexBefore] = baseTabWidth - (lineWidthSoFar % baseTabWidth);
			}

			lineWidthSoFar += glyphWidths[glyphIndexBefore];
		}

		compute_line_visual_runs(result, sbParagraph, paragraphText, paragraphLength, lineStart, lineEnd,
				highestRun, highestRunCharEnd, vertical);
	}

	return highestRun;
}

void LayoutBuilder::shape_logical_run(const SingleScriptFont& font, const char* paragraphText,
		int32_t offset, int32_t count, int32_t paragraphStart, int32_t paragraphLength, int script,
		const icu::Locale& locale, bool reversed, bool vertical) {
	auto hbScript = hb_script_from_string(uscript_getShortName(static_cast<UScriptCode>(script)), 4);
	auto direction = vertical ? (reversed ? HB_DIRECTION_BTT : HB_DIRECTION_TTB)
			: (reversed ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
	auto primaryAxis = static_cast<size_t>(vertical);
	auto secondaryAxis = static_cast<size_t>(!vertical);

	hb_buffer_clear_contents(m_buffer);

	hb_buffer_set_script(m_buffer, hbScript);
	hb_buffer_set_language(m_buffer, hb_language_from_string(locale.getLanguage(), -1));
	hb_buffer_set_direction(m_buffer, direction);
	hb_buffer_set_flags(m_buffer, (hb_buffer_flags_t)((offset == 0 ? HB_BUFFER_FLAG_BOT : 0)
			| (offset + count == paragraphLength ? HB_BUFFER_FLAG_EOT : 0)));

	icu::Edits edits;

	if (font.syntheticSmallCaps) {
		std::string upperStr;
		icu::StringByteSink<std::string> sink(&upperStr);
		UErrorCode errc{};

		// FIXME: To produce accurate shaping results, harfbuzz needs +-5 characters around the substring,
		// if available. These should be provided within the upperStr
		icu::CaseMap::utf8ToUpper(uscript_getName(static_cast<UScriptCode>(script)), 0,
				{paragraphText + offset, count}, sink, &edits, errc);
		hb_buffer_add_utf8(m_buffer, upperStr.data(), (int)upperStr.size(), 0, (int)upperStr.size());
		count = (int32_t)upperStr.size();
	}
	else {
		hb_buffer_add_utf8(m_buffer, paragraphText, paragraphLength, offset, 0);
		hb_buffer_add_utf8(m_buffer, paragraphText + offset, paragraphLength - offset, 0, count);
	}

	std::vector<hb_feature_t> features;
	maybe_add_tag_runs(features, HB_TAG('s', 'm', 'c', 'p'), count, font.smallcaps, font.syntheticSmallCaps);
	maybe_add_tag_runs(features, HB_TAG('s', 'u', 'b', 's'), count, font.subscript, font.syntheticSubscript);
	maybe_add_tag_runs(features, HB_TAG('s', 'u', 'p', 's'), count, font.superscript,
			font.syntheticSuperscript);

	auto fontData = FontRegistry::get_font_data(font);
	hb_shape(fontData.hbFont, m_buffer, features.data(), static_cast<unsigned>(features.size()));

	auto glyphCount = hb_buffer_get_length(m_buffer);
	auto* glyphPositions = hb_buffer_get_glyph_positions(m_buffer, nullptr);
	auto* glyphInfos = hb_buffer_get_glyph_infos(m_buffer, nullptr);

	if (font.syntheticSmallCaps) {
		remap_char_indices(glyphInfos, glyphCount, edits, paragraphText + offset, reversed);
	}

	auto* glyphPositionData = reinterpret_cast<const hb_position_t*>(glyphPositions);
	auto* pOffsetPrimary = glyphPositionData
			+ (offsetof(hb_glyph_position_t, x_offset) / sizeof(hb_position_t));
	auto* pAdvancePrimary = glyphPositionData
			+ (offsetof(hb_glyph_position_t, x_advance) / sizeof(hb_position_t));
	auto* pOffsetSecondary = glyphPositionData
			+ (offsetof(hb_glyph_position_t, y_offset) / sizeof(hb_position_t));
	auto* pAdvanceSecondary = glyphPositionData
			+ (offsetof(hb_glyph_position_t, y_advance) / sizeof(hb_position_t));

	static constexpr const size_t stride = sizeof(hb_glyph_position_t) / sizeof(hb_position_t);

	if (vertical) {
		std::swap(pOffsetPrimary, pOffsetSecondary);
		std::swap(pAdvancePrimary, pAdvanceSecondary);
	}

	auto glyphPosStartIndex = m_glyphPositions[secondaryAxis].size();

	for (unsigned i = 0; i < glyphCount; ++i) {
		m_glyphPositions[secondaryAxis].emplace_back(m_cursor + *pOffsetSecondary);
		m_cursor += *pAdvanceSecondary;

		if (paragraphText[glyphInfos[i].cluster + offset] == '\t') {
			glyphPositions[i].x_advance = fontData.spaceAdvance;
			glyphInfos[i].codepoint = fontData.spaceGlyphIndex;
		}

		pOffsetSecondary += stride;
		pAdvanceSecondary += stride;
	}

	int32_t widthMultiplier = vertical ? -1 : 1;

	if (reversed) {
		for (unsigned i = glyphCount; i--;) {
			m_glyphs.emplace_back(glyphInfos[i].codepoint);
			m_charIndices.emplace_back(glyphInfos[i].cluster + offset + paragraphStart);

			auto width = pAdvancePrimary[i * stride] - pOffsetPrimary[i * stride];
			if (i != glyphCount - 1) {
				width += pOffsetPrimary[(i + 1) * stride];
			}
			m_glyphPositions[primaryAxis].emplace_back(width * widthMultiplier);
		}

		std::reverse(m_glyphPositions[secondaryAxis].begin() + glyphPosStartIndex,
				m_glyphPositions[secondaryAxis].end());
	}
	else {
		for (unsigned i = 0; i < glyphCount; ++i) {
			m_glyphs.emplace_back(glyphInfos[i].codepoint);
			m_charIndices.emplace_back(glyphInfos[i].cluster + offset + paragraphStart);

			auto width = pAdvancePrimary[i * stride] - pOffsetPrimary[i * stride];
			if (i != glyphCount - 1) {
				width += pOffsetPrimary[(i + 1) * stride];
			}
			m_glyphPositions[primaryAxis].emplace_back(width * widthMultiplier);
		}
	}
}

void LayoutBuilder::compute_line_visual_runs(LayoutInfo& result, SBParagraphRef sbParagraph, const char* chars,
		int32_t count, int32_t lineStart, int32_t lineEnd, size_t& highestRun, int32_t& highestRunCharEnd,
		bool vertical) {
	SBLineRef sbLine = SBParagraphCreateLine(sbParagraph, lineStart, lineEnd - lineStart);
	auto runCount = SBLineGetRunCount(sbLine);
	auto* sbRuns = SBLineGetRunsPtr(sbLine);
	float maxAscent{};
	float maxDescent{};
	int32_t visualRunWidth{};

	for (int32_t i = 0; i < runCount; ++i) {
		int32_t logicalStart, runLength;
		bool reversed = sbRuns[i].level & 1;
		auto runStart = sbRuns[i].offset;
		auto runEnd = runStart + sbRuns[i].length - 1;

		if (!reversed) {
			auto run = binary_search(0, m_logicalRuns.size(), [&](auto index) {
				return m_logicalRuns[index].charEndIndex <= runStart;
			});
			auto chrIndex = runStart;

			for (;;) {
				auto logicalRunEnd = m_logicalRuns[run].charEndIndex;
				auto fontData = FontRegistry::get_font_data(m_logicalRuns[run].font);

				if (auto ascent = fontData.get_ascent(); ascent > maxAscent) {
					maxAscent = ascent;
				}

				if (auto descent = fontData.get_descent(); descent < maxDescent) {
					maxDescent = descent;
				}

				if (runEnd < logicalRunEnd) {
					append_visual_run(result, run, chrIndex, runEnd, visualRunWidth, highestRun,
							highestRunCharEnd, reversed, vertical);
					break;
				}
				else {
					append_visual_run(result, run, chrIndex, logicalRunEnd - 1, visualRunWidth, highestRun,
							highestRunCharEnd, reversed, vertical);
					chrIndex = logicalRunEnd;
					++run;
				}
			}
		}
		else {
			auto run = binary_search(0, m_logicalRuns.size(), [&](auto index) {
				return m_logicalRuns[index].charEndIndex <= runEnd;
			});
			auto chrIndex = runEnd;

			for (;;) {
				auto logicalRunStart = run == 0 ? 0 : m_logicalRuns[run - 1].charEndIndex;
				auto fontData = FontRegistry::get_font_data(m_logicalRuns[run].font);

				if (auto ascent = fontData.get_ascent(); ascent > maxAscent) {
					maxAscent = ascent;
				}

				if (auto descent = fontData.get_descent(); descent < maxDescent) {
					maxDescent = descent;
				}

				if (runStart >= logicalRunStart) {
					append_visual_run(result, run, runStart, chrIndex, visualRunWidth, highestRun,
							highestRunCharEnd, reversed, vertical);
					break;
				}
				else {
					append_visual_run(result, run, logicalRunStart, chrIndex, visualRunWidth, highestRun,
							highestRunCharEnd, reversed, vertical);
					chrIndex = logicalRunStart - 1;
					--run;
				}
			}
		}
	}

	result.append_line(maxAscent - maxDescent, maxAscent);
	
	SBLineRelease(sbLine);
}

void LayoutBuilder::append_visual_run(LayoutInfo& result, size_t run, int32_t charStartIndex,
		int32_t charEndIndex, int32_t& visualRunWidth, size_t& highestRun, int32_t& highestRunCharEnd,
		bool reversed, bool vertical) {
	auto logicalFirstGlyph = run == 0 ? 0 : m_logicalRuns[run - 1].glyphEndIndex;
	auto logicalLastGlyph = m_logicalRuns[run].glyphEndIndex;
	auto primaryAxis = static_cast<size_t>(vertical);
	auto secondaryAxis = static_cast<size_t>(!vertical);
	uint32_t visualFirstGlyph;
	uint32_t visualLastGlyph;

	if (charEndIndex > highestRunCharEnd) {
		highestRun = result.get_run_count();
		highestRunCharEnd = charEndIndex;
	}

	visualFirstGlyph = binary_search(logicalFirstGlyph, logicalLastGlyph - logicalFirstGlyph, [&](auto index) {
		return m_charIndices[index] < charStartIndex;
	});

	visualLastGlyph = binary_search(visualFirstGlyph, logicalLastGlyph - visualFirstGlyph, [&](auto index) {
		return m_charIndices[index] <= charEndIndex;
	});

	if (reversed) {
		for (uint32_t i = visualLastGlyph; i-- > visualFirstGlyph;) {
			result.append_glyph(m_glyphs[i]);
			result.append_char_index(m_charIndices[i]);

			float pos[] = {scalbnf(visualRunWidth, -6), scalbnf(m_glyphPositions[secondaryAxis][i], -6)};
			result.append_glyph_position(pos[primaryAxis], pos[secondaryAxis]);
			visualRunWidth += m_glyphPositions[primaryAxis][i];
		}

		float pos[] = {scalbnf(visualRunWidth, -6),
			scalbnf(m_glyphPositions[secondaryAxis][visualLastGlyph], -6)};
		result.append_glyph_position(pos[primaryAxis], pos[secondaryAxis]);
	}
	else {
		for (uint32_t i = visualFirstGlyph; i < visualLastGlyph; ++i) {
			result.append_glyph(m_glyphs[i]);
			result.append_char_index(m_charIndices[i]);

			float pos[] = {scalbnf(visualRunWidth, -6), scalbnf(m_glyphPositions[secondaryAxis][i], -6)};
			result.append_glyph_position(pos[primaryAxis], pos[secondaryAxis]);
			visualRunWidth += m_glyphPositions[primaryAxis][i];
		}

		float pos[] = {scalbnf(visualRunWidth, -6),
			scalbnf(m_glyphPositions[secondaryAxis][visualLastGlyph], -6)};
		result.append_glyph_position(pos[primaryAxis], pos[secondaryAxis]);
	}

	result.append_run(m_logicalRuns[run].font, static_cast<uint32_t>(charStartIndex),
			static_cast<uint32_t>(charEndIndex + 1), reversed);
}

void LayoutBuilder::apply_tab_widths_no_line_break(const char* fullText, int32_t tabWidthFixed,
		bool tabWidthFromPixels, int32_t* glyphWidths) {
	size_t runIndex = 0;
	int32_t lineWidthSoFar = 0;

	for (size_t i = 0; i < m_charIndices.size(); ++i) {
		runIndex += m_logicalRuns[runIndex].glyphEndIndex == i;

		if (fullText[m_charIndices[i]] == '\t') {
			auto baseTabWidth = tabWidthFromPixels ? tabWidthFixed : mul_fixed(glyphWidths[i], tabWidthFixed);
			glyphWidths[i] = baseTabWidth - (lineWidthSoFar % baseTabWidth);
		}

		lineWidthSoFar += glyphWidths[i];
	}
}

void LayoutBuilder::reset(size_t capacity) {
	m_glyphs.clear();
	m_glyphs.reserve(capacity);

	m_charIndices.clear();
	m_charIndices.reserve(capacity);

	m_glyphPositions[0].clear();
	m_glyphPositions[0].reserve(capacity + 1);
	m_glyphPositions[1].clear();
	m_glyphPositions[1].reserve(capacity + 1);

	m_cursor = 0;

	m_logicalRuns.clear();
}

// Static Functions

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

