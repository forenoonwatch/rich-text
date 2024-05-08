#include "string_transformations.hpp"

#include <sstream>

#include <unicode/utf8.h>

using namespace Text;

// Public Functions

Pair<std::string, std::vector<uint32_t>> Text::string_to_upper(std::string_view str) {
	std::ostringstream ss;
	std::vector<uint32_t> indexMap;
	UChar32 chr;
	int32_t charIndex = 0;

	for (; charIndex < str.size();) {
		auto lastCharIndex = charIndex;
		U8_NEXT(str.data(), charIndex, str.size(), chr);

		if (chr <= 0) {
			break;
		}

		char writeBuffer[4] = {};

		if (chr >= 'a' && chr <= 'z') {
			ss.put(chr - 'a' + 'A');
			indexMap.emplace_back(static_cast<uint32_t>(lastCharIndex));
		}
		// Latin small letter sharp S
		else if (chr == 0xDF) {
			ss.put('S');
			ss.put('S');
			indexMap.emplace_back(static_cast<uint32_t>(lastCharIndex));
			indexMap.emplace_back(static_cast<uint32_t>(lastCharIndex));
		}
		else {
			int32_t writeCount = 0;
			bool writeError = false;
			U8_APPEND(writeBuffer, writeCount, 4, chr, writeError);
			ss.write(writeBuffer, writeCount);

			for (int32_t i = 0; i < writeCount; ++i) {
				indexMap.emplace_back(static_cast<uint32_t>(lastCharIndex));
			}
		}
	}

	return {ss.str(), std::move(indexMap)};
}

