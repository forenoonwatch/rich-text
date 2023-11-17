#include "paragraph_layout.hpp"
#include "utf_conversion_util.hpp"

using namespace Text;

static void convert_char_indices(std::vector<uint32_t>& charIndices, const char16_t* srcChars,
		int32_t srcCharCount, const char* dstChars, int32_t dstCharCount);
static void convert_runs(std::vector<VisualRun>& runs, const char16_t* srcChars,
		int32_t srcCharCount, const char* dstChars, int32_t dstCharCount);

// Public Functions

void Text::convert_paragraph_layout_to_utf8(ParagraphLayout& result, const char16_t* srcChars,
		int32_t srcCharCount, const char* dstChars, int32_t dstCharCount) {
	convert_char_indices(result.charIndices, srcChars, srcCharCount, dstChars, dstCharCount);
	convert_runs(result.visualRuns, srcChars, srcCharCount, dstChars, dstCharCount);
}

// Static Functions

static void convert_char_indices(std::vector<uint32_t>& charIndices, const char16_t* srcChars,
		int32_t srcCharCount, const char* dstChars, int32_t dstCharCount) {
	for (size_t i = 0; i < charIndices.size(); ++i) {
		charIndices[i] = utf16_index_to_utf8(srcChars, srcCharCount, dstChars, dstCharCount, charIndices[i]);
	}
}

static void convert_runs(std::vector<VisualRun>& runs, const char16_t* srcChars,
		int32_t srcCharCount, const char* dstChars, int32_t dstCharCount) {
	for (size_t i = 0; i < runs.size(); ++i) {
		auto& run = runs[i];
		auto highChar = utf16_index_to_utf8(srcChars, srcCharCount, dstChars, dstCharCount,
				run.charEndIndex + run.charEndOffset);
		auto lowChar = utf16_index_to_utf8(srcChars, srcCharCount, dstChars, dstCharCount, run.charEndIndex);
		run.charStartIndex = utf16_index_to_utf8(srcChars, srcCharCount, dstChars, dstCharCount,
				run.charStartIndex);
		run.charEndIndex = lowChar;
		run.charEndOffset = highChar - lowChar;
	}
}

