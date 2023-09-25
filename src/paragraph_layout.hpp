#pragma once

#include "common.hpp"
#include "text_runs.hpp"

#include <cstdint>

namespace icu_73 { class LEFontInstance; }

class MultiScriptFont;

enum class ParagraphLayoutFlags : uint8_t {
	NONE = 0,
	RIGHT_TO_LEFT = 1, // Whether the text direction default should be RTL. Leave unset to default to LTR
	OVERRIDE_DIRECTIONALITY = 2, // Whether the configured text direction should override script directions
	VERTICAL = 4, // Whether the text is composed vertically. Leave unset for horizontal text.
};

ZN_DEFINE_ENUM_BITFLAG_OPERATORS(ParagraphLayoutFlags)

struct VisualRun {
	const icu_73::LEFontInstance* pFont;
	uint32_t charEndIndex;
	uint32_t glyphPositionEndIndex;
	bool rightToLeft;
};

struct LineInfo {
	uint32_t visualRunsEndIndex;
	float width;
	float ascent;
	float height;
};

struct ParagraphLayout {
	std::vector<VisualRun> visualRuns;
	std::vector<LineInfo> lines;
	std::vector<uint32_t> glyphIndices;
	std::vector<uint32_t> glyphToCharMap;
	std::vector<float> glyphPositions;
	bool rightToLeft;
};

void build_paragraph_layout(ParagraphLayout& result, const char16_t* chars, int32_t count,
		const RichText::TextRuns<const MultiScriptFont*>& fontRuns, float lineWidth, ParagraphLayoutFlags flags);

