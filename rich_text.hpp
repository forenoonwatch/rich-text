#pragma once

#include "color.hpp"
#include "text_runs.hpp"

#include <unicode/unistr.h>

#include <string>

class Font;

namespace RichText {

struct Result {
	icu::UnicodeString str;
	TextRuns<const Font*> fontRuns;
	TextRuns<Color> colorRuns;
	TextRuns<bool> strikethroughRuns;
	TextRuns<bool> underlineRuns;
};

Result parse(const std::string& text, Font& baseFont, Color baseColor);

}

