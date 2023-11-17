#include "paragraph_layout.hpp"

#include "multi_script_font.hpp"

#include <unicode/utext.h>
#include <unicode/utf16.h>

#include <layout/ParagraphLayout.h>

using namespace Text;

static constexpr const UChar32 CH_LF = 0x000A;
static constexpr const UChar32 CH_CR = 0x000D;
static constexpr const UChar32 CH_LSEP = 0x2028;
static constexpr const UChar32 CH_PSEP = 0x2029;

static void handle_line_icu_lx(ParagraphLayout& result, icu::ParagraphLayout::Line& line, int32_t charOffset,
		const char16_t* chars, size_t& highestRun, int32_t& highestRunCharEnd);

// Public Functions

void build_paragraph_layout_icu_lx(ParagraphLayout& result, const char16_t* chars, int32_t count,
		const ValueRuns<const MultiScriptFont*>& fontRuns, float textAreaWidth, float textAreaHeight,
		TextYAlignment textYAlignment, ParagraphLayoutFlags flags) {
	ValueRuns<const MultiScriptFont*> subsetFontRuns(fontRuns.get_run_count());

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
						subsetFontRuns.get_limits(), subsetFontRuns.get_run_count());
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

