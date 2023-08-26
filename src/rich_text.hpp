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

Result make_default_runs(const std::string& text, std::string& contentText, const Font& baseFont,
		Color baseColor);
Result parse(const std::string& text, std::string& contentText, const Font& baseFont, Color baseColor);

}

