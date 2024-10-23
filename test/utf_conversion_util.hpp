#pragma once

#include <unicode/utf8.h>
#include <unicode/utf16.h>

constexpr uint32_t utf16_index_to_utf8(const char16_t* srcChars, int32_t srcCharCount,
		const char* dstChars, int32_t dstCharCount, uint32_t targetIndex, uint32_t& srcCounter,
		uint32_t& dstCounter) {
	while (srcCounter < targetIndex) {
		U16_FWD_1(srcChars, srcCounter, srcCharCount);
		U8_FWD_1(dstChars, dstCounter, dstCharCount);
	}

	return dstCounter;
}

constexpr uint32_t utf16_index_to_utf8(const char16_t* srcChars, int32_t srcCharCount,
		const char* dstChars, int32_t dstCharCount, uint32_t targetIndex) {
	uint32_t srcCounter{};
	uint32_t dstCounter{};
	return utf16_index_to_utf8(srcChars, srcCharCount, dstChars, dstCharCount, targetIndex, srcCounter,
			dstCounter);
}

constexpr uint32_t utf8_index_to_utf16(const char* srcChars, int32_t srcCharCount,
		const char16_t* dstChars, int32_t dstCharCount, uint32_t targetIndex, uint32_t& srcCounter,
		uint32_t& dstCounter) {
	while (srcCounter < targetIndex) {
		U8_FWD_1(srcChars, srcCounter, srcCharCount);
		U16_FWD_1(dstChars, dstCounter, dstCharCount);
	}

	return dstCounter;
}

constexpr uint32_t utf8_index_to_utf16(const char* srcChars, int32_t srcCharCount, const char16_t* dstChars,
		int32_t dstCharCount, uint32_t targetIndex) {
	uint32_t srcCounter{};
	uint32_t dstCounter{};
	return utf8_index_to_utf16(srcChars, srcCharCount, dstChars, dstCharCount, targetIndex, srcCounter,
			dstCounter);
}

