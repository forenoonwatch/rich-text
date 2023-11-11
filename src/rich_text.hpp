#pragma once

#include "color.hpp"
#include "text_runs.hpp"
#include "multi_script_font.hpp"
#include "stroke_type.hpp"

#include <string>

namespace RichText {

struct StrokeState {
	Color color;
	uint8_t thickness;
	StrokeType joins;
};

struct Result {
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

void convert_runs_to_utf16(Result& runs, const std::string& contentText, const char16_t* dstText, 
		int32_t dstTextLength);

}

