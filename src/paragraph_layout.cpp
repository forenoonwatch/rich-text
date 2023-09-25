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
		const RichText::TextRuns<const MultiScriptFont*>& fontRuns, float lineWidth, ParagraphLayoutFlags flags);
static void handle_line_icu(ParagraphLayout& result, icu::ParagraphLayout::Line& line);

void build_paragraph_layout(ParagraphLayout& result, const char16_t* chars, int32_t count,
		const RichText::TextRuns<const MultiScriptFont*>& fontRuns, float lineWidth, ParagraphLayoutFlags flags) {
	build_paragraph_layout_icu(result, chars, count, fontRuns, lineWidth, flags);
}

static void build_paragraph_layout_icu(ParagraphLayout& result, const char16_t* chars, int32_t count,
		const RichText::TextRuns<const MultiScriptFont*>& fontRuns, float lineWidth, ParagraphLayoutFlags flags) {
	RichText::TextRuns<const MultiScriptFont*> subsetFontRuns(fontRuns.get_value_count());

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

				while (auto* pLine = pl.nextLine(lineWidth)) {
					handle_line_icu(result, *pLine);
					delete pLine;
				}
			}
			else {
				// FIXME: A MultiScriptFont returns 0 for ascent and descent
				auto* pFont = fontRuns.get_value(byteIndex);

				result.lines.push_back({
					.visualRunsEndIndex = static_cast<uint32_t>(result.visualRuns.size()),
					.width = 0.f,
					.ascent = static_cast<float>(pFont->getAscent()),
					.height = static_cast<float>(pFont->getAscent() + pFont->getDescent()),
				});
			}

			if (c == U_SENTINEL) {
				break;
			}
			else if (c == CH_CR && UTEXT_CURRENT32(&iter) == CH_LF) {
				UTEXT_NEXT32(&iter);
			}

			byteIndex = UTEXT_GETNATIVEINDEX(&iter);
		}
	}
}

static void handle_line_icu(ParagraphLayout& result, icu::ParagraphLayout::Line& line) {
	result.visualRuns.reserve(result.visualRuns.size() + line.countRuns());

	int32_t maxAscent = 0;
	int32_t maxDescent = 0;
	int32_t charCount = 0;

	auto charBaseIndex = static_cast<uint32_t>(result.glyphIndices.size());
	auto glyphPositionBaseIndex = static_cast<uint32_t>(result.glyphPositions.size()); 

	auto charEndIndex = charBaseIndex;
	auto glyphPositionEndIndex = glyphPositionBaseIndex;

	for (int32_t i = 0; i < line.countRuns(); ++i) {
		auto* pRun = line.getVisualRun(i);
		auto ascent = pRun->getAscent();
		auto descent = pRun->getDescent();

		if (ascent > maxAscent) {
			maxAscent = ascent;
		}

		if (descent > maxDescent) {
			maxDescent = descent;
		}

		charEndIndex += pRun->getGlyphCount();
		glyphPositionEndIndex += 2 * pRun->getGlyphCount() + 2;

		result.visualRuns.push_back({
			.pFont = pRun->getFont(),
			.charEndIndex = charEndIndex,
			.glyphPositionEndIndex = glyphPositionEndIndex,
			.rightToLeft = pRun->getDirection() == UBIDI_RTL,
		});
	}

	result.glyphIndices.resize(charEndIndex);
	result.glyphToCharMap.resize(charEndIndex);
	result.glyphPositions.resize(glyphPositionEndIndex);

	for (int32_t i = 0; i < line.countRuns(); ++i) {
		auto* pRun = line.getVisualRun(i);

		std::memcpy(&result.glyphIndices[charBaseIndex], pRun->getGlyphs(),
				pRun->getGlyphCount() * sizeof(uint32_t));
		std::memcpy(&result.glyphToCharMap[charBaseIndex], pRun->getGlyphToCharMap(),
				pRun->getGlyphCount() * sizeof(uint32_t));
		std::memcpy(&result.glyphPositions[charBaseIndex], pRun->getPositions(),
				(2 * pRun->getGlyphCount() + 2) * sizeof(uint32_t));

		charBaseIndex += pRun->getGlyphCount();
		glyphPositionBaseIndex += 2 * pRun->getGlyphCount() + 2;
	}

	result.lines.push_back({
		.visualRunsEndIndex = static_cast<uint32_t>(result.visualRuns.size()),
		.width = static_cast<float>(line.getWidth()),
		.ascent = static_cast<float>(maxAscent),
		.height = static_cast<float>(maxAscent + maxDescent),
	});
}

