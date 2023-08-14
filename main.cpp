#include <cstdio>
#include <cstring>

#include <string_view>

#include <layout/ParagraphLayout.h>

#include "font_instance.hpp"
#include "file_read_bytes.hpp"

int main() {
	std::u16string_view testText{u"Hello World"};
	std::unique_ptr<FontInstance> font = FontInstance::create("C:/Windows/Fonts/segoeui.ttf");

	size_t fileSize{};
	std::unique_ptr<char[]> fileData;
	if (!(fileData = file_read_bytes("Sample.txt", fileSize))) {
		return 1;
	}

	auto str = icu::UnicodeString::fromUTF8(std::string_view(fileData.get(), fileSize));
	
	icu::FontRuns fontRuns(1);
	fontRuns.add(font.get(), str.length());
	LEErrorCode err{};
	icu::ParagraphLayout pl(str.getBuffer(), str.length(), &fontRuns, nullptr, nullptr, nullptr,
			UBIDI_DEFAULT_LTR, false, err);

	int lineCount = 0;
	while (auto* line = pl.nextLine(100.f)) {
		for (le_int32 i = 0; i < line->countRuns(); ++i) {
			auto* run = line->getVisualRun(i);
			printf("Line %d run %d has %d glyphs\n", lineCount, i, run->getGlyphCount());
		}

		delete line;
		++lineCount;
	}

	puts("ICUTest - Done");
}
