#pragma once

#include "font.hpp"
#include "rich_text.hpp"

#include <layout/ParagraphLayout.h>
#include <layout/RunArrays.h>

namespace Text {

constexpr const UChar32 CH_LF = 0x000A;
constexpr const UChar32 CH_CR = 0x000D;
constexpr const UChar32 CH_LSEP = 0x2028;
constexpr const UChar32 CH_PSEP = 0x2029;

struct LayoutInfo {
	std::vector<std::unique_ptr<icu::ParagraphLayout::Line>> lines;
	RichText::TextRuns<int32_t> offsetRunsByLine;
	float lineY;
	float lineHeight;
	UBiDiLevel paragraphLevel;
};

void build_line_layout_info(RichText::Result& textInfo, float lineWidth, LayoutInfo& layoutInfo);
int32_t get_line_char_start_index(const icu::ParagraphLayout::Line*, int32_t charOffset); 
int32_t get_line_char_end_index(const icu::ParagraphLayout::Line*, int32_t charOffset); 
float get_cursor_offset_in_line(const icu::ParagraphLayout::Line*, int32_t cursorIndex);
float get_line_end_position(const icu::ParagraphLayout::Line*);

int32_t find_line_start_containing_index(const LayoutInfo&, int32_t index);
int32_t find_line_end_containing_index(const LayoutInfo&, int32_t index, int32_t textEnd, icu::BreakIterator&);

template <typename Functor>
void for_each_line(const LayoutInfo& info, float lineWidth, Functor&& func) {
	auto lineY = info.lineY;

	for (size_t lineNumber = 0; lineNumber < info.lines.size(); ++lineNumber) {
		auto charOffset = info.offsetRunsByLine.get_value(static_cast<int32_t>(lineNumber));
		auto* pLine = info.lines[lineNumber].get();
		auto lineX = pLine && info.paragraphLevel == UBIDI_RTL ? lineWidth - pLine->getWidth() : 0.f;

		func(lineNumber, pLine, charOffset, lineX, lineY);

		lineY += info.lineHeight;
	}
}

template <typename Functor>
void for_each_run(const LayoutInfo& info, float lineWidth, Functor&& func) {
	for_each_line(info, lineWidth, [&](auto /*lineNumber*/, auto* pLine, auto charOffset, auto lineX,
			auto lineY) {
		if (pLine) {
			for (le_int32 runID = 0; runID < pLine->countRuns(); ++runID) {
				auto* run = pLine->getVisualRun(runID);
				func(*run, charOffset, lineX, lineY);
			}
		}
	});
}

template <typename Functor>
void for_each_glyph(const LayoutInfo& info, float lineWidth, Functor&& func) {
	for_each_run(info, lineWidth, [&](auto& run, auto charOffset, auto lineX, auto lineY) {
		auto* pFont = static_cast<const Font*>(run.getFont());
		auto* posData = run.getPositions();
		auto* glyphs = run.getGlyphs();
		auto* glyphChars = run.getGlyphToCharMap();

		for (le_int32 i = 0; i < run.getGlyphCount(); ++i) {
			if (glyphs[i] == 0xFFFFu || glyphs[i] == 0xFFFEu) {
				continue;
			}

			auto globalCharIndex = glyphChars[i] + charOffset;

			func(glyphs[i], globalCharIndex, posData + 2 * i, *pFont, lineX, lineY);
		}
	});
}

}

