#pragma once

#include "color.hpp"
#include "text_runs.hpp"
#include "multi_script_font.hpp"
#include "stroke_type.hpp"

#include <unicode/unistr.h>

#include <string>

namespace RichText {

struct StrokeState {
	Color color;
	uint8_t thickness;
	StrokeType joins;
};

struct Result {
	icu::UnicodeString str;
	TextRuns<const MultiScriptFont*> fontRuns;
	TextRuns<Color> colorRuns;
	TextRuns<StrokeState> strokeRuns;
	TextRuns<bool> strikethroughRuns;
	TextRuns<bool> underlineRuns;
	std::vector<MultiScriptFont> ownedFonts;
};

Result make_default_runs(const std::string& text, std::string& contentText, const MultiScriptFont& baseFont,
		Color baseColor, const StrokeState& baseStroke);
Result parse(const std::string& text, std::string& contentText, const MultiScriptFont& baseFont, Color baseColor,
		const StrokeState& baseStroke);

}

