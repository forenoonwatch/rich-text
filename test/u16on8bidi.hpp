#pragma once

#include <unicode/ubidi.h>

struct U16On8BiDi;

U16On8BiDi* u16on8bidi_open();
void u16on8bidi_close(U16On8BiDi* pBiDi);

void u16on8bidi_setInverse(U16On8BiDi* pBiDi, UBool isInverse);
UBool u16on8bidi_isInverse(U16On8BiDi* pBiDi);

void u16on8bidi_orderParagraphsLTR(U16On8BiDi* pBiDi, UBool orderParagraphsLTR);
UBool u16on8bidi_isOrderParagraphsLTR(U16On8BiDi* pBiDi);

void u16on8bidi_setReorderingMode(U16On8BiDi* pBiDi, UBiDiReorderingMode reorderingMode);
UBiDiReorderingMode u16on8bidi_getReorderingMode(U16On8BiDi* pBiDi);

void u16on8bidi_setReorderingOptions(U16On8BiDi* pBiDi, uint32_t reorderingOptions);
uint32_t u16on8bidi_getReorderingOptions(U16On8BiDi* pBiDi);

void u16on8bidi_setContext(U16On8BiDi* pBiDi, const UChar* prologue, int32_t proLength, const UChar* epilogue,
		int32_t epiLength, UErrorCode* pErrorCode);
void u16on8bidi_setPara(U16On8BiDi* pBiDi, const char16_t* text, int32_t length, UBiDiLevel paraLevel,
		UBiDiLevel* embeddingLevels, UErrorCode* pErrorCode);
void u16on8bidi_setLine(const U16On8BiDi* pParaBiDi, int32_t start, int32_t limit, U16On8BiDi* pLineBiDi,
		UErrorCode* pErrorCode);

UBiDiDirection u16on8bidi_getDirection(const U16On8BiDi* pBiDi);
const UChar* u16on8bidi_getText(const U16On8BiDi* pBiDi);
int32_t u16on8bidi_getLength(const U16On8BiDi* pBiDi);
UBiDiLevel u16on8bidi_getParaLevel(const U16On8BiDi* pBiDi);
int32_t u16on8bidi_countParagraphs(const U16On8BiDi* pBiDi);
int32_t u16on8bidi_getParagraph(const U16On8BiDi* pBiDi, int32_t charIndex, int32_t* pParaStart,
		int32_t* pParaLimit, UBiDiLevel* pParaLevel, UErrorCode* pErrorCode);
void u16on8bidi_getParagraphByIndex(const U16On8BiDi* pBiDi, int32_t paraIndex, int32_t* pParaStart,
		int32_t* pParaLimit, UBiDiLevel* pParaLevel, UErrorCode* pErrorCode);
UBiDiLevel u16on8bidi_getLevelAt(const U16On8BiDi* pBiDi, int32_t charIndex);
const UBiDiLevel* u16on8bidi_getLevels(const U16On8BiDi* pBiDi, UErrorCode* pErrorCode);

void u16on8bidi_getLogicalRun(const U16On8BiDi* pBiDi, int32_t logicalPosition, int32_t* pLogicalLimit,
		UBiDiLevel* pLevel);
int32_t u16on8bidi_countRuns(U16On8BiDi* pBiDi, UErrorCode* pErrorCode);
UBiDiDirection u16on8bidi_getVisualRun(U16On8BiDi* pBiDi, int32_t runIndex, int32_t* pLogicalStart,
		int32_t* pLength);
int32_t u16on8bidi_getVisualIndex(U16On8BiDi* pBiDi, int32_t logicalIndex, UErrorCode* pErrorCode);
int32_t u16on8bidi_getLogicalIndex(U16On8BiDi* pBiDi, int32_t visualIndex, UErrorCode* pErrorCode);
void u16on8bidi_getLogicalMap(U16On8BiDi* pBiDi, int32_t* indexMap, UErrorCode* pErrorCode);
void u16on8bidi_getVisualMap(U16On8BiDi* pBiDi, int32_t* indexMap, UErrorCode* pErrorCode);

int32_t u16on8bidi_getProcessedLength(const U16On8BiDi* pBiDi);
int32_t u16on8bidi_getResultLength(const U16On8BiDi* pBiDi);

UCharDirection u16on8bidi_getCustomizedClass(U16On8BiDi* pBiDi, UChar32 c);
void u16on8bidi_setClassCallback(U16On8BiDi* pBiDi, UBiDiClassCallback* newFn, const void* newContext,
		UBiDiClassCallback** oldFn, const void** oldContext, UErrorCode* pErrorCode);
void u16on8bidi_getClassCallback(U16On8BiDi* pBiDi, UBiDiClassCallback** fn, const void** context);

int32_t u16on8bidi_writeReordered(U16On8BiDi* pBiDi, UChar* dest, int32_t destSize, uint16_t options,
		UErrorCode* pErrorCode);

