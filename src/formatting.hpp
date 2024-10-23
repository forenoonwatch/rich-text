#pragma once

#include "color.hpp"
#include "value_runs.hpp"
#include "font.hpp"
#include "stroke_type.hpp"

#include <string>

namespace Text {

struct StrokeState {
	Color color;
	uint8_t thickness;
	StrokeType joins;
};

struct FormattingRuns {
	ValueRuns<Font> fontRuns;
	ValueRuns<Color> colorRuns;
	ValueRuns<StrokeState> strokeRuns;
	ValueRuns<bool> strikethroughRuns;
	ValueRuns<bool> underlineRuns;
	ValueRuns<bool> smallcapsRuns;
	ValueRuns<bool> subscriptRuns;
	ValueRuns<bool> superscriptRuns;
};

FormattingRuns make_default_formatting_runs(const std::string& text, std::string& contentText,
		Font baseFont, Color baseColor, const StrokeState& baseStroke);
FormattingRuns parse_inline_formatting(const std::string& text, std::string& contentText, 
		Font baseFont, Color baseColor, const StrokeState& baseStroke);

}

