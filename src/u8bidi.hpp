#pragma once

#include <unicode/ubidi.h>

struct U8BiDi;

U8BiDi* u8bidi_open();
void u8bidi_close(U8BiDi* pBiDi);

void u8bidi_set_paragraph(U8BiDi* pBiDi, const char* text, int32_t length, UBiDiLevel paraLevel,
		UBiDiLevel* embeddingLevels, UErrorCode* pErrorCode);
void u8bidi_set_line(const U8BiDi* pParaBiDi, int32_t start, int32_t limit, U8BiDi* pLineBiDi,
		UErrorCode* pErrorCode);

void u8bidi_order_paragraphs_ltr(U8BiDi* pBiDi, UBool orderParagraphsLTR);

void u8bidi_set_reordering_mode(U8BiDi* pBiDi, UBiDiReorderingMode reorderingMode);

void u8bidi_set_reordering_options(U8BiDi* pBiDi, uint32_t reorderingOptions);

UBiDiDirection u8bidi_get_direction(const U8BiDi* pBiDi);
int32_t u8bidi_get_length(const U8BiDi* pBiDi);
UBiDiLevel u8bidi_get_paragraph_level(const U8BiDi* pBiDi);
UBiDiLevel u8bidi_get_level_at(const U8BiDi* pBiDi, int32_t charIndex);
int32_t u8bidi_get_paragraph(const U8BiDi* pBiDi, int32_t charIndex, int32_t* pParaStart, int32_t* pParaLimit,
		UBiDiLevel* pParaLevel, UErrorCode* pErrorCode);
void u8bidi_get_paragraph_by_index(const U8BiDi* pBiDi, int32_t paraIndex, int32_t* pParaStart,
		int32_t* pParaLimit, UBiDiLevel* pParaLevel, UErrorCode* pErrorCode);
const UBiDiLevel* u8bidi_get_levels(U8BiDi* pBiDi, UErrorCode* pErrorCode);

void u8bidi_get_logical_run(const U8BiDi* pBiDi, int32_t logicalPosition, int32_t* pLogicalLimit,
		UBiDiLevel* pLevel);
int32_t u8bidi_count_runs(U8BiDi* pBiDi, UErrorCode* pErrorCode);
UBiDiDirection u8bidi_get_visual_run(U8BiDi* pBiDi, int32_t runIndex, int32_t* pLogicalStart, int32_t* pLength);
int32_t u8bidi_get_visual_index(U8BiDi* pBiDi, int32_t logicalIndex, UErrorCode* pErrorCode);
int32_t u8bidi_get_logical_index(U8BiDi* pBiDi, int32_t visualIndex, UErrorCode* pErrorCode);
void u8bidi_get_logical_map(U8BiDi* pBiDi, int32_t* indexMap, UErrorCode* pErrorCode);
void u8bidi_get_visual_map(U8BiDi* pBiDi, int32_t* indexMap, UErrorCode* pErrorCode);

int32_t u8bidi_get_result_length(const U8BiDi* pBiDi);

UCharDirection u8bidi_get_customized_class(U8BiDi* pBiDi, UChar32 c);

int32_t u8bidi_write_reordered(U8BiDi* pBiDi, char* dest, int32_t destSize, uint16_t options,
		UErrorCode* pErrorCode);

UBool u8bidi_get_runs(U8BiDi* pBiDi, UErrorCode*);

UBiDiLevel u8bidi_get_para_level_at_index(const U8BiDi* pBiDi, int32_t index);


