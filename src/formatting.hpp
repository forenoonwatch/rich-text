#pragma once

#include "color.hpp"
#include "value_runs.hpp"
#include "multi_script_font.hpp"
#include "stroke_type.hpp"

#include <string>

namespace Text {

struct StrokeState {
	Color color;
	uint8_t thickness;
	StrokeType joins;
};

struct FormattingRuns {
	ValueRuns<const MultiScriptFont*> fontRuns;
	ValueRuns<Color> colorRuns;
	ValueRuns<StrokeState> strokeRuns;
	ValueRuns<bool> strikethroughRuns;
	ValueRuns<bool> underlineRuns;
	std::vector<MultiScriptFont> ownedFonts;
};

FormattingRuns make_default_formatting_runs(const std::string& text, std::string& contentText,
		const MultiScriptFont& baseFont, Color baseColor, const StrokeState& baseStroke);
FormattingRuns parse_inline_formatting(const std::string& text, std::string& contentText, 
		const MultiScriptFont& baseFont, Color baseColor, const StrokeState& baseStroke);

void convert_formatting_runs_to_utf16(FormattingRuns& runs, const std::string& contentText,
		const char16_t* dstText, int32_t dstTextLength);

}

