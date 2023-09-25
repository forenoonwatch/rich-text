#include "text_utils.hpp"

#include <unicode/utext.h>

static bool find_offset_in_run_ltr(const icu::ParagraphLayout::VisualRun& run, int32_t cursorIndex,
		float& outOffset);
static bool find_offset_in_run_rtl(const icu::ParagraphLayout::VisualRun& run, int32_t cursorIndex,
		float& outOffset);

static float find_cluster_position_ltr(const icu::ParagraphLayout::VisualRun& run, int32_t glyphIndex);
static float find_cluster_position_rtl(const icu::ParagraphLayout::VisualRun& run, int32_t glyphIndex);

static bool find_position_in_run(const icu::ParagraphLayout::VisualRun& run, float cursorX, int32_t& result);
static bool find_position_in_run_ltr(const icu::ParagraphLayout::VisualRun& run, float cursorX, int32_t& result);
static bool find_position_in_run_rtl(const icu::ParagraphLayout::VisualRun& run, float cursorX, int32_t& result);

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

int32_t Text::get_leftmost_char_index(const icu::ParagraphLayout::Line* pLine, int32_t charOffset,
		icu::BreakIterator& iter) {
	if (pLine && pLine->countRuns() > 0) {
		auto* pFirstRun = pLine->getVisualRun(0);
		auto* firstRunChars = pFirstRun->getGlyphToCharMap();
		auto idx = firstRunChars[0] + charOffset;
		return pFirstRun->getDirection() == UBIDI_LTR ? idx : iter.following(idx);
	}
	else {
		return charOffset;
	}
}

int32_t Text::get_rightmost_char_index(const icu::ParagraphLayout::Line* pLine, int32_t charOffset,
		icu::BreakIterator& iter) {
	if (pLine && pLine->countRuns() > 0) {
		auto* pLastRun = pLine->getVisualRun(pLine->countRuns() - 1);
		auto* lastRunChars = pLastRun->getGlyphToCharMap();
		auto idx = lastRunChars[pLastRun->getGlyphCount() - 1] + charOffset;
		return pLastRun->getDirection() == UBIDI_LTR ? iter.following(idx) : idx;
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
		float offset;
		// FIXME: I don't need to iterate the entire run to know if the cursor is in it
		bool found = run->getDirection() == UBIDI_RTL ? find_offset_in_run_rtl(*run, cursorIndex, offset)
				: find_offset_in_run_ltr(*run, cursorIndex, offset);

		if (found) {
			return offset;
		}
	}

	// FIXME: Assert unreachable?
	return 0.f;
}

float Text::get_line_end_position(const icu::ParagraphLayout::Line* pLine) {
	if (pLine && pLine->countRuns() > 0) {
		auto* run = pLine->getVisualRun(pLine->countRuns() - 1);
		return run->getDirection() == UBIDI_RTL ? run->getPositions()[0]
				: run->getPositions()[2 * run->getGlyphCount()];
	}
	else {
		return 0.f;
	}
}

int32_t Text::find_line_start_containing_index(const LayoutInfo& info, int32_t index) {
	for (size_t lineNumber = 0; lineNumber < info.lines.size(); ++lineNumber) {
		auto charOffset = info.offsetRunsByLine.get_value(static_cast<int32_t>(lineNumber));
		auto* pLine = info.lines[lineNumber].get();

		if (pLine) {
			auto lineStart = get_line_char_start_index(pLine, charOffset);
			auto lineEnd = get_line_char_end_index(pLine, charOffset);

			if (index >= lineStart && lineNumber < info.lines.size() - 1) {
				auto* pNextLine = info.lines[lineNumber + 1].get();
				auto nextOffset = info.offsetRunsByLine.get_value(static_cast<int32_t>(lineNumber + 1));
				auto nextStart = get_line_char_start_index(pNextLine, nextOffset);

				if (index < nextStart) {
					return lineStart;
				}
			}
			else if (index >= lineStart) {
				return lineStart;
			}
		}
		else if (index == charOffset) {
			return charOffset;
		}
	}

	return {};
}

int32_t Text::find_line_end_containing_index(const LayoutInfo& info, int32_t index, int32_t textEnd,
		icu::BreakIterator& iter) {
	for (size_t lineNumber = 0; lineNumber < info.lines.size(); ++lineNumber) {
		auto charOffset = info.offsetRunsByLine.get_value(static_cast<int32_t>(lineNumber));
		auto* pLine = info.lines[lineNumber].get();

		if (pLine) {
			auto lineStart = get_line_char_start_index(pLine, charOffset);
			auto lineEnd = get_line_char_end_index(pLine, charOffset);

			if (index >= lineStart && lineNumber < info.lines.size() - 1) {
				auto* pNextLine = info.lines[lineNumber + 1].get();
				auto nextOffset = info.offsetRunsByLine.get_value(static_cast<int32_t>(lineNumber + 1));
				auto nextStart = get_line_char_start_index(pNextLine, nextOffset);

				if (index < nextStart) {
					return iter.following(lineEnd);
				}
			}
			else if (index >= lineStart) {
				return textEnd;
			}
		}
		else if (index == charOffset) {
			return charOffset;
		}
	}

	return {};
}

int32_t Text::find_closest_cursor_position(const LayoutInfo& info, float textWidth,
		TextXAlignment textXAlignment, int32_t textLength, icu::BreakIterator& iter, size_t lineNumber,
		float cursorX) {
	auto* pLine = info.lines[lineNumber].get();
	auto charOffset = info.offsetRunsByLine.get_value(static_cast<int32_t>(lineNumber));
	auto lineX = get_line_x_start(info, textWidth, textXAlignment, pLine);
	float lineEndPos = lineX;
	if (pLine) {
		auto* lastRun = pLine->getVisualRun(pLine->countRuns() - 1);
		auto* lastRunsPositions = lastRun->getPositions();
		lineEndPos = lineX + std::max(lastRunsPositions[0], lastRunsPositions[2 * lastRun->getGlyphCount()]);
	}
	int32_t result = 0;

	if (cursorX <= lineX) {
		return get_leftmost_char_index(pLine, charOffset, iter);
	}
	else if (cursorX >= lineEndPos) {
		return get_rightmost_char_index(pLine, charOffset, iter);
	}

	if (pLine) {
		for (le_int32 runID = 0; runID < pLine->countRuns(); ++runID) {
			auto* run = pLine->getVisualRun(runID);

			if (find_position_in_run(*run, cursorX - lineX, result)) {
				if (result == run->getGlyphCount()) {
					if (runID < pLine->countRuns() - 1) {
						return charOffset + pLine->getVisualRun(runID + 1)->getGlyphToCharMap()[result];
					}
					else {
						return run->getDirection() == UBIDI_LTR
								? iter.following(get_line_char_end_index(pLine, charOffset))
								: get_line_char_start_index(pLine, charOffset);
					}
				}
				else {
					return charOffset + run->getGlyphToCharMap()[result];
				}
			}
		}
	}
	else {
		return charOffset;
	}

	return result;
}

float Text::get_line_x_start(const LayoutInfo& info, float textWidth, TextXAlignment align,
		const icu::ParagraphLayout::Line* pLine) {
	float lineWidth = pLine ? pLine->getWidth() : 0.f;

	switch (align) {
		case TextXAlignment::LEFT:
			return info.paragraphLevel == UBIDI_RTL ? textWidth - lineWidth : 0.f;
		case TextXAlignment::RIGHT:
			return textWidth - lineWidth;
		case TextXAlignment::CENTER:
			return 0.5f * (textWidth - lineWidth);
	}

	// FIXME: Assert unreachable
	return 0.f;
}

float Text::get_text_height(const LayoutInfo& info) {
	return info.lineHeight * static_cast<float>(info.lines.size());
}

// Static Functions

static bool find_offset_in_run_ltr(const icu::ParagraphLayout::VisualRun& run, int32_t cursorIndex,
		float& outOffset) {
	auto* glyphs = run.getGlyphs();
	auto* glyphChars = run.getGlyphToCharMap();
	auto* posData = run.getPositions();

	for (le_int32 i = 0; i < run.getGlyphCount(); ++i) {
		if (glyphChars[i] == cursorIndex) {
			if (glyphs[i] == 0xFFFF) {
				outOffset = find_cluster_position_ltr(run, i);
			}
			else {
				outOffset = posData[2 * i];
			}

			return true;
		}
	}

	return false;
}

static bool find_offset_in_run_rtl(const icu::ParagraphLayout::VisualRun& run, int32_t cursorIndex,
		float& outOffset) {
	auto* glyphs = run.getGlyphs();
	auto* glyphChars = run.getGlyphToCharMap();
	auto* posData = run.getPositions();

	for (le_int32 i = 0; i < run.getGlyphCount(); ++i) {
		if (glyphChars[i] == cursorIndex) {
			if (glyphs[i] == 0xFFFF || (i > 0 && glyphs[i - 1] == 0xFFFF)) {
				outOffset = find_cluster_position_rtl(run, i);
			}
			else {
				outOffset = posData[2 * i + 2];
			}

			return true;
		}
	}

	return false;
}

static float find_cluster_position_ltr(const icu::ParagraphLayout::VisualRun& run, int32_t glyphIndex) {
	auto* glyphs = run.getGlyphs();
	auto* glyphChars = run.getGlyphToCharMap();
	auto* posData = run.getPositions();

	auto clusterStart = glyphIndex;
	auto clusterEnd = glyphIndex;

	while (clusterStart > 0 && glyphs[clusterStart] == 0xFFFF) {
		--clusterStart;
	}

	while (clusterEnd < run.getGlyphCount() && glyphs[clusterEnd] == 0xFFFF) {
		++clusterEnd;
	}

	auto startPos = posData[2 * clusterStart];
	auto endPos = posData[2 * clusterEnd];

	return startPos + (endPos - startPos) * static_cast<float>(glyphIndex - clusterStart)
			/ static_cast<float>(clusterEnd - clusterStart);
}

static float find_cluster_position_rtl(const icu::ParagraphLayout::VisualRun& run, int32_t glyphIndex) {
	auto* glyphs = run.getGlyphs();
	auto* glyphChars = run.getGlyphToCharMap();
	auto* posData = run.getPositions();

	auto clusterStart = glyphIndex - (glyphIndex != 0);
	auto clusterEnd = glyphIndex;

	while (clusterStart > 0 && glyphs[clusterStart] == 0xFFFF) {
		--clusterStart;
	}

	while (clusterEnd < run.getGlyphCount() && glyphs[clusterEnd] == 0xFFFF) {
		++clusterEnd;
	}

	++clusterEnd;

	auto startPos = posData[2 * clusterStart];
	auto endPos = posData[2 * clusterEnd];

	return startPos + (endPos - startPos) * static_cast<float>(glyphIndex + 1 - clusterStart)
			/ static_cast<float>(clusterEnd - clusterStart);
}

static bool find_position_in_run(const icu::ParagraphLayout::VisualRun& run, float cursorX, int32_t& result) {
	return run.getDirection() == UBIDI_RTL ? find_position_in_run_rtl(run, cursorX, result)
			: find_position_in_run_ltr(run, cursorX, result);
}

static bool find_position_in_run_ltr(const icu::ParagraphLayout::VisualRun& run, float cursorX,
		int32_t& result) {
	auto* posData = run.getPositions();
	auto* glyphs = run.getGlyphs();
	auto* glyphChars = run.getGlyphToCharMap();

	for (le_int32 i = 0; i < run.getGlyphCount(); ++i) {
		// Cluster
		if (i < run.getGlyphCount() - 1 && glyphs[i + 1] == 0xFFFF) {
			auto clusterEnd = i + 1;
			while (clusterEnd < run.getGlyphCount() && glyphs[clusterEnd] == 0xFFFF) {
				++clusterEnd;
			}

			auto posX = posData[2 * i];
			auto nextPosX = posData[2 * clusterEnd];

			if (cursorX >= posX && cursorX <= nextPosX) {
				result = i + static_cast<int32_t>((clusterEnd - i) * (cursorX - posX) / (nextPosX - posX)
						+ 0.5f);
				return true;
			}

			i = clusterEnd - 1;
		}
		else {
			auto posX = posData[2 * i];
			auto nextPosX = posData[2 * i + 2];

			if (cursorX >= posX && cursorX <= nextPosX) {
				result = cursorX - posX < nextPosX - cursorX ? i : i + 1;
				return true;
			}
		}
	}

	return false;
}

static bool find_position_in_run_rtl(const icu::ParagraphLayout::VisualRun& run, float cursorX,
		int32_t& result) {
	auto* posData = run.getPositions();
	auto* glyphs = run.getGlyphs();
	auto* glyphChars = run.getGlyphToCharMap();

	for (le_int32 i = run.getGlyphCount(); i > 0; --i) {
		// Cluster
		if (i > 2 && glyphs[i - 2] == 0xFFFF) {
			auto clusterStart = i - 1;
			auto clusterEnd = i - 2;
			while (clusterEnd > 0 && glyphs[clusterEnd] == 0xFFFF) {
				--clusterEnd;
			}

			auto posX = posData[2 * clusterEnd];
			auto nextPosX = posData[2 * clusterStart + 2];

			if (cursorX >= posX && cursorX <= nextPosX) {
				auto clusterSize = clusterStart - clusterEnd + 1;
				result = clusterEnd + static_cast<int32_t>(clusterSize * (cursorX - posX) / (nextPosX - posX)
						+ 0.5f) - 1;
				return true;
			}

			i = clusterEnd;
		}
		else {
			auto posX = posData[2 * i - 2];
			auto nextPosX = posData[2 * i];

			if (cursorX >= posX && cursorX <= nextPosX) {
				result = cursorX - posX <= nextPosX - cursorX ? i - 2 : i - 1;
				return true;
			}
		}
	}

	return false;
}

