#include "paragraph_layout.hpp"

#include <unicode/utext.h>

void Text::build_line_layout_info(RichText::Result& textInfo, float lineWidth, LayoutInfo& layoutInfo) {
	RichText::TextRuns<const MultiScriptFont*> subsetFontRuns(textInfo.fontRuns.get_value_count());

	auto* start = textInfo.str.getBuffer();
	auto* end = start + textInfo.str.length();
	UText iter UTEXT_INITIALIZER;
	UErrorCode err{};
	utext_openUnicodeString(&iter, &textInfo.str, &err);

	int32_t byteIndex = 0;

	int32_t maxAscent = 0;
	int32_t maxDescent = 0;
	layoutInfo.paragraphLevel = UBIDI_DEFAULT_LTR;

	for (;;) {
		auto idx = UTEXT_GETNATIVEINDEX(&iter);
		auto c = UTEXT_NEXT32(&iter);

		if (c == U_SENTINEL || c == CH_LF || c == CH_CR || c == CH_LSEP || c == CH_PSEP) {
			if (idx != byteIndex) {
				auto byteCount = idx - byteIndex;

				subsetFontRuns.clear();
				textInfo.fontRuns.get_runs_subset(byteIndex, byteCount, subsetFontRuns);

				LEErrorCode err{};
				auto** ppFonts = const_cast<const MultiScriptFont**>(subsetFontRuns.get_values());
				icu::FontRuns fontRuns(reinterpret_cast<const icu::LEFontInstance**>(ppFonts),
						subsetFontRuns.get_limits(), subsetFontRuns.get_value_count());
				icu::ParagraphLayout pl(textInfo.str.getBuffer() + byteIndex, byteCount, &fontRuns, nullptr,
						nullptr, nullptr, layoutInfo.paragraphLevel, false, err);

				if (layoutInfo.paragraphLevel == UBIDI_DEFAULT_LTR) {
					layoutInfo.paragraphLevel = pl.getParagraphLevel();
				}

				auto ascent = pl.getAscent();
				auto descent = pl.getDescent();

				if (ascent > maxAscent) {
					maxAscent = ascent;
				}

				if (descent > maxDescent) {
					maxDescent = descent;
				}

				while (auto* pLine = pl.nextLine(lineWidth)) {
					layoutInfo.lines.emplace_back(pLine);
				}

				layoutInfo.offsetRunsByLine.add(static_cast<int32_t>(layoutInfo.lines.size()), byteIndex);
			}
			else {
				layoutInfo.lines.emplace_back();
				layoutInfo.offsetRunsByLine.add(static_cast<int32_t>(layoutInfo.lines.size()), byteIndex);
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

	layoutInfo.lineY = static_cast<float>(maxAscent);
	layoutInfo.lineHeight = static_cast<float>(maxDescent + maxAscent);
}

int32_t Text::get_line_char_start_index(const icu::ParagraphLayout::Line* pLine, int32_t charOffset) {
	if (pLine && pLine->countRuns() > 0) {
		auto* pFirstRun = pLine->getVisualRun(0);
		auto* firstRunChars = pFirstRun->getGlyphToCharMap();
		return std::min(firstRunChars[0], firstRunChars[pFirstRun->getGlyphCount() - 1]) + charOffset;
	}
	else {
		return charOffset;
	}
}

int32_t Text::get_line_char_end_index(const icu::ParagraphLayout::Line* pLine, int32_t charOffset) {
	if (pLine && pLine->countRuns() > 0) {
		auto* pLastRun = pLine->getVisualRun(pLine->countRuns() - 1);
		auto* lastRunChars = pLastRun->getGlyphToCharMap();
		return std::max(lastRunChars[0], lastRunChars[pLastRun->getGlyphCount() - 1]) + charOffset;
	}
	else {
		return charOffset;
	}
}

float Text::get_cursor_offset_in_line(const icu::ParagraphLayout::Line* pLine, int32_t cursorIndex) {
	if (!pLine || pLine->countRuns() == 0) {
		return 0.f;
	}

	for (le_int32 runID = 0; runID < pLine->countRuns(); ++runID) {
		auto* run = pLine->getVisualRun(runID);
		auto* glyphChars = run->getGlyphToCharMap();
		auto* posData = run->getPositions();

		// FIXME: This might never be true with a compound glyph, need to find greatest char <= cursorIndex
		// instead
		for (le_int32 i = 0; i < run->getGlyphCount(); ++i) {
			if (glyphChars[i] == cursorIndex) {
				return posData[2 * i];
			}
		}
	}

	// FIXME: Assert unreachable?
	return 0.f;
}

float Text::get_line_end_position(const icu::ParagraphLayout::Line* pLine) {
	if (pLine && pLine->countRuns() > 0) {
		auto* run = pLine->getVisualRun(pLine->countRuns() - 1);
		return run->getPositions()[2 * run->getGlyphCount()];
	}
	else {
		return 0.f;
	}
}

