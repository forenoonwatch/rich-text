#include "paragraph_layout.hpp"
#include "utf_conversion_util.hpp"

static void convert_char_indices(std::vector<uint32_t>& charIndices, const char16_t* srcChars,
		int32_t srcCharCount, const char* dstChars, int32_t dstCharCount);
static void convert_lines(std::vector<LineInfo>& lines, const char16_t* srcChars,
		int32_t srcCharCount, const char* dstChars, int32_t dstCharCount);

// Public Functions

void convert_paragraph_layout_to_utf8(ParagraphLayout& result, const char16_t* srcChars, int32_t srcCharCount,
		const char* dstChars, int32_t dstCharCount) {
	convert_char_indices(result.charIndices, srcChars, srcCharCount, dstChars, dstCharCount);
	convert_lines(result.lines, srcChars, srcCharCount, dstChars, dstCharCount);
}

// Static Functions

static void convert_char_indices(std::vector<uint32_t>& charIndices, const char16_t* srcChars,
		int32_t srcCharCount, const char* dstChars, int32_t dstCharCount) {
	uint32_t charIndex8{};
	uint32_t charIndex16{};

	for (size_t i = 0; i < charIndices.size(); ++i) {
		charIndices[i] = utf16_index_to_utf8(srcChars, srcCharCount, dstChars, dstCharCount, charIndices[i],
				charIndex16, charIndex8);
	}
}

static void convert_lines(std::vector<LineInfo>& lines, const char16_t* srcChars,
		int32_t srcCharCount, const char* dstChars, int32_t dstCharCount) {
	uint32_t charIndex8{};
	uint32_t charIndex16{};

	for (size_t i = 0; i < lines.size(); ++i) {
		auto diffedIndex = utf16_index_to_utf8(srcChars, srcCharCount, dstChars, dstCharCount,
				lines[i].lastStringIndex - lines[i].lastCharDiff, charIndex16, charIndex8);

		if (lines[i].lastCharDiff == 0) {
			lines[i].lastStringIndex = diffedIndex;
		}
		else {
			lines[i].lastStringIndex = utf16_index_to_utf8(srcChars, srcCharCount, dstChars, dstCharCount,
					lines[i].lastStringIndex, charIndex16, charIndex8);
			lines[i].lastCharDiff = lines[i].lastStringIndex - diffedIndex;
		}
	}
}

