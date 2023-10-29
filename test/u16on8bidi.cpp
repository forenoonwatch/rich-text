#include "u16on8bidi.hpp"
#include "u8bidi.hpp"
#include "utf_conversion_util.hpp"

#include <unicode/ustring.h>
#include <cstdlib>

struct U16On8BiDi {
	U8BiDi* pBiDi;
	const char16_t* srcChars;
	int32_t srcLength;
	char* buffer;
	int32_t bufferLength;

	int32_t to_utf8(int32_t index) const {
		return utf16_index_to_utf8(srcChars, srcLength, buffer, bufferLength, index);
	}

	int32_t to_utf16(int32_t index) const {
		return utf8_index_to_utf16(buffer, bufferLength, srcChars, srcLength, index);
	}
};

static void make_utf8_string(U16On8BiDi* pBiDi, const char16_t* text, int32_t length);

// Public Functions

U16On8BiDi* u16on8bidi_open() {
	auto* pBiDi = new U16On8BiDi{};
	pBiDi->pBiDi = u8bidi_open();
	pBiDi->buffer = nullptr;
	return pBiDi;
}

void u16on8bidi_close(U16On8BiDi* pBiDi) {
	if (pBiDi->buffer) {
		std::free(pBiDi->buffer);
	}

	if (pBiDi->pBiDi) {
		u8bidi_close(pBiDi->pBiDi);
	}

	delete pBiDi;
}

void u16on8bidi_setPara(U16On8BiDi* pBiDi, const char16_t *text, int32_t length, UBiDiLevel paraLevel,
		UBiDiLevel *embeddingLevels, UErrorCode *pErrorCode) {
	make_utf8_string(pBiDi, text, length);
	pBiDi->srcChars = text;
	pBiDi->srcLength = length;
	u8bidi_set_paragraph(pBiDi->pBiDi, pBiDi->buffer, pBiDi->bufferLength, paraLevel, embeddingLevels,
			pErrorCode);
}

void u16on8bidi_setLine(const U16On8BiDi* pParaBiDi, int32_t start, int32_t limit, U16On8BiDi* pLineBiDi,
		UErrorCode* pErrorCode) {
	start = pParaBiDi->to_utf8(start);
	limit = pParaBiDi->to_utf8(limit);
	u8bidi_set_line(pParaBiDi->pBiDi, start, limit, pLineBiDi->pBiDi, pErrorCode);
}

void u16on8bidi_orderParagraphsLTR(U16On8BiDi* pBiDi, UBool orderParagraphsLTR) {
	u8bidi_order_paragraphs_ltr(pBiDi->pBiDi, orderParagraphsLTR);
}

void u16on8bidi_setReorderingMode(U16On8BiDi* pBiDi, UBiDiReorderingMode reorderingMode) {
	u8bidi_set_reordering_mode(pBiDi->pBiDi, reorderingMode);
}

void u16on8bidi_setReorderingOptions(U16On8BiDi* pBiDi, uint32_t reorderingOptions) {
	u8bidi_set_reordering_options(pBiDi->pBiDi, reorderingOptions);
}

UBiDiDirection u16on8bidi_getDirection(const U16On8BiDi* pBiDi) {
	return u8bidi_get_direction(pBiDi->pBiDi);
}

int32_t u16on8bidi_getLength(const U16On8BiDi* pBiDi) {
	return pBiDi->to_utf16(u8bidi_get_length(pBiDi->pBiDi));
}

UBiDiLevel u16on8bidi_getParaLevel(const U16On8BiDi* pBiDi) {
	return u8bidi_get_paragraph_level(pBiDi->pBiDi);
}

UBiDiLevel u16on8bidi_getLevelAt(const U16On8BiDi* pBiDi, int32_t charIndex) {
	charIndex = pBiDi->to_utf8(charIndex);
	return u8bidi_get_level_at(pBiDi->pBiDi, charIndex);
}

const UBiDiLevel* u16on8bidi_getLevels(const U16On8BiDi* pBiDi, UErrorCode* pErrorCode) {
	return u8bidi_get_levels(pBiDi->pBiDi, pErrorCode);
}

void u16on8bidi_getLogicalRun(const U16On8BiDi* pBiDi, int32_t logicalPosition, int32_t* pLogicalLimit,
		UBiDiLevel* pLevel) {
	logicalPosition = pBiDi->to_utf8(logicalPosition);
	u8bidi_get_logical_run(pBiDi->pBiDi, logicalPosition, pLogicalLimit, pLevel);
	*pLogicalLimit = pBiDi->to_utf16(*pLogicalLimit);
}

int32_t u16on8bidi_countRuns(U16On8BiDi* pBiDi, UErrorCode* pErrorCode) {
	return u8bidi_count_runs(pBiDi->pBiDi, pErrorCode);
}

UBiDiDirection u16on8bidi_getVisualRun(U16On8BiDi* pBiDi, int32_t runIndex, int32_t* pLogicalStart,
		int32_t* pLength) {
	auto result = u8bidi_get_visual_run(pBiDi->pBiDi, runIndex, pLogicalStart, pLength);
	auto logicalEnd = pBiDi->to_utf16(*pLogicalStart + *pLength);
	*pLogicalStart = pBiDi->to_utf16(*pLogicalStart);
	*pLength = logicalEnd - *pLogicalStart;
	return result;
}

int32_t u16on8bidi_getVisualIndex(U16On8BiDi* pBiDi, int32_t logicalIndex, UErrorCode* pErrorCode) {
	logicalIndex = pBiDi->to_utf8(logicalIndex);
	return pBiDi->to_utf16(u8bidi_get_visual_index(pBiDi->pBiDi, logicalIndex, pErrorCode));
}

int32_t u16on8bidi_getLogicalIndex(U16On8BiDi* pBiDi, int32_t visualIndex, UErrorCode* pErrorCode) {
	visualIndex = pBiDi->to_utf8(visualIndex);
	return pBiDi->to_utf16(u8bidi_get_logical_index(pBiDi->pBiDi, visualIndex, pErrorCode));
}

void u16on8bidi_getLogicalMap(U16On8BiDi* pBiDi, int32_t* indexMap, UErrorCode* pErrorCode) {
	// FIXME: Have to convert the map
	u8bidi_get_logical_map(pBiDi->pBiDi, indexMap, pErrorCode);
}

void u16on8bidi_getVisualMap(U16On8BiDi* pBiDi, int32_t* indexMap, UErrorCode* pErrorCode) {
	// FIXME: Have to convert the map
	u8bidi_get_visual_map(pBiDi->pBiDi, indexMap, pErrorCode);
}

int32_t u16on8bidi_getResultLength(const U16On8BiDi* pBiDi) {
	// FIXME: convert to UTF-16?
	return u8bidi_get_result_length(pBiDi->pBiDi);
}

int32_t u16on8bidi_writeReordered(U16On8BiDi* pBiDi, UChar* dest, int32_t destSize, uint16_t options,
		UErrorCode* pErrorCode) {
	auto* buffer = (char*)std::malloc(pBiDi->bufferLength);
	int32_t reorderLength = u8bidi_write_reordered(pBiDi->pBiDi, buffer, pBiDi->bufferLength, options,
			pErrorCode);
	int32_t result{};
	u_strFromUTF8(dest, destSize, &result, buffer, reorderLength, pErrorCode);
	std::free(buffer);
	return result;
}

// Static Functions

static void make_utf8_string(U16On8BiDi* pBiDi, const char16_t* text, int32_t length) {
	if (pBiDi->buffer) {
		std::free(pBiDi->buffer);
	}

	if (length < 0) {
		length = u_strlen(text);
	}

	// Allocate worst case size
	pBiDi->buffer = (char*)std::malloc(length * 4);
	UErrorCode errc{};
	u_strToUTF8(pBiDi->buffer, 4 * length, &pBiDi->bufferLength, text, length, &errc);
}

