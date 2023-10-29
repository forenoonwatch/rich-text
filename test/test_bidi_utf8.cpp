#include <catch2/catch_test_macros.hpp>

#include "bidi_test.hpp"
#include "u16on8bidi.hpp"

#include <unicode/ustring.h>
#include <unicode/ucnv.h>
#include <unicode/ushape.h>
#include <cstring.h>

#define MAXLEN      MAX_STRING_LENGTH

static void initCharFromDirProps();
static UChar* getStringFromDirProps(const uint8_t *dirProps, int32_t length, UChar *buffer);

static int pseudoToU16(const int length, const char * input, UChar * output);
static int u16ToPseudo(const int length, const UChar * input, char * output);
static void checkWhatYouCan(U16On8BiDi *bidi, const char *srcChars, const char *dstChars);

static void doTests(U16On8BiDi *pBiDi, U16On8BiDi *pLine, UBool countRunsFirst);
static void doTest(U16On8BiDi *pBiDi, int testNumber, const BiDiTestData *test, int32_t lineStart,
		UBool countRunsFirst);
static void doMisc();

static void _testReordering(U16On8BiDi *pBiDi, int testNumber);
static void _testInverseBidi(U16On8BiDi *pBiDi, const UChar *src, int32_t srcLength, UBiDiLevel direction,
		UErrorCode *pErrorCode);
static void _testManyInverseBidi(U16On8BiDi *pBiDi, UBiDiLevel direction);
static void _testWriteReverse();
static void _testManyAddedPoints();
static void _testMisc();

TEST_CASE("TestBiDi", "[U8BiDi]") {
	U16On8BiDi *pBiDi=NULL, *pLine=NULL;
    UErrorCode errorCode=U_ZERO_ERROR;

    pBiDi = u16on8bidi_open();
	REQUIRE(pBiDi != nullptr);
	pLine = u16on8bidi_open();
	REQUIRE(pLine != nullptr);

	doTests(pBiDi, pLine, false);
	doTests(pBiDi, pLine, true);
    doMisc();

    if (pLine!=NULL) {
        u16on8bidi_close(pLine);
    }
    if (pBiDi!=NULL) {
        u16on8bidi_close(pBiDi);
    }
}

// Static Functions

static void doTests(U16On8BiDi *pBiDi, U16On8BiDi *pLine, UBool countRunsFirst) {
    UChar string[MAXLEN];
    int32_t lineStart;
    UBiDiLevel paraLevel;

    for (int testNumber = 0; testNumber < bidiTestCount; ++testNumber) {
        UErrorCode errorCode = U_ZERO_ERROR;
        getStringFromDirProps(tests[testNumber].text, tests[testNumber].length, string);
        paraLevel=tests[testNumber].paraLevel;
        u16on8bidi_setPara(pBiDi, string, -1, paraLevel, NULL, &errorCode);
		REQUIRE(U_SUCCESS(errorCode));

		lineStart=tests[testNumber].lineStart;

		if (lineStart==-1) {
			doTest(pBiDi, testNumber, tests+testNumber, 0, countRunsFirst);
		}
		else {
			u16on8bidi_setLine(pBiDi, lineStart, tests[testNumber].lineLimit, pLine, &errorCode);
			REQUIRE(U_SUCCESS(errorCode));
			doTest(pLine, testNumber, tests+testNumber, lineStart, countRunsFirst);
		}
    }
}

static void doTest(U16On8BiDi *pBiDi, int testNumber, const BiDiTestData *test, int32_t lineStart,
		UBool countRunsFirst) {
    const uint8_t *dirProps= (test->text == NULL) ? NULL : test->text+lineStart;
    const UBiDiLevel *levels=test->levels;
    const uint8_t *visualMap=test->visualMap;
    int32_t i, len=u16on8bidi_getLength(pBiDi), logicalIndex, runCount = 0;
    UErrorCode errorCode=U_ZERO_ERROR;
    UBiDiLevel level, level2;

    if (countRunsFirst) {
        runCount = u16on8bidi_countRuns(pBiDi, &errorCode);
		REQUIRE(U_SUCCESS(errorCode));
    }

    _testReordering(pBiDi, testNumber);

    REQUIRE(test->direction == u16on8bidi_getDirection(pBiDi));
    REQUIRE(test->resultLevel == u16on8bidi_getParaLevel(pBiDi));

    for(i=0; i<len; ++i) {
        REQUIRE(levels[i] == u16on8bidi_getLevelAt(pBiDi, i));
    }

    for(i=0; i<len; ++i) {
        logicalIndex=u16on8bidi_getVisualIndex(pBiDi, i, &errorCode);
		REQUIRE(U_SUCCESS(errorCode));
		REQUIRE(visualMap[i] == logicalIndex);
    }

    if (!countRunsFirst) {
        runCount=u16on8bidi_countRuns(pBiDi, &errorCode);
		REQUIRE(U_SUCCESS(errorCode));
    }

    for(logicalIndex=0; logicalIndex<len;) {
        level=u16on8bidi_getLevelAt(pBiDi, logicalIndex);
        u16on8bidi_getLogicalRun(pBiDi, logicalIndex, &logicalIndex, &level2);
		CHECK(level == level2);
		REQUIRE(--runCount >= 0);
    }

	REQUIRE(runCount == 0);
}

static void doMisc() {
	/* Miscellaneous tests to exercize less popular code paths */
    U16On8BiDi *bidi, *bidiLine;
    UChar src[MAXLEN], dest[MAXLEN];
    int32_t srcLen, destLen, runCount, i;
    UBiDiLevel level;
    UBiDiDirection dir;
    int32_t map[MAXLEN];
    UErrorCode errorCode=U_ZERO_ERROR;
    static const int32_t srcMap[6] = {0,1,-1,5,4};
    static const int32_t dstMap[6] = {0,1,-1,-1,4,3};

    bidi = u16on8bidi_open();
	REQUIRE(bidi != nullptr);

    bidiLine = u16on8bidi_open();
	REQUIRE(bidiLine != nullptr);

    destLen = ubidi_writeReverse(src, 0, dest, MAXLEN, 0, &errorCode);
	CHECK(destLen == 0);
	REQUIRE(U_SUCCESS(errorCode));

    u16on8bidi_setPara(bidi, src, 0, UBIDI_LTR, NULL, &errorCode);
    destLen = u16on8bidi_writeReordered(bidi, dest, MAXLEN, 0, &errorCode);
	REQUIRE(destLen == 0);
    REQUIRE(U_SUCCESS(errorCode));

    srcLen = u_unescape("abc       ", src, MAXLEN);
    u16on8bidi_setPara(bidi, src, srcLen, UBIDI_RTL, NULL, &errorCode);
    u16on8bidi_setLine(bidi, 0, 6, bidiLine, &errorCode);
    for (i = 3; i < 6; i++) {
        level = u16on8bidi_getLevelAt(bidiLine, i);
		CHECK(level == UBIDI_RTL);
    }
    REQUIRE(U_SUCCESS(errorCode));

    srcLen = u_unescape("abc       def", src, MAXLEN);
    u16on8bidi_setPara(bidi, src, srcLen, UBIDI_RTL, NULL, &errorCode);
    u16on8bidi_setLine(bidi, 0, 6, bidiLine, &errorCode);
    for (i = 3; i < 6; i++) {
        level = u16on8bidi_getLevelAt(bidiLine, i);
		CHECK(level == UBIDI_RTL);
    }
    REQUIRE(U_SUCCESS(errorCode));

    srcLen = u_unescape("abcdefghi    ", src, MAXLEN);
    u16on8bidi_setPara(bidi, src, srcLen, UBIDI_RTL, NULL, &errorCode);
    u16on8bidi_setLine(bidi, 0, 6, bidiLine, &errorCode);
    for (i = 3; i < 6; i++) {
        level = u16on8bidi_getLevelAt(bidiLine, i);
		CHECK(level == 2);
    }
    REQUIRE(U_SUCCESS(errorCode));

    u16on8bidi_setReorderingOptions(bidi, UBIDI_OPTION_REMOVE_CONTROLS);
    srcLen = u_unescape("\\u200eabc       def", src, MAXLEN);
    u16on8bidi_setPara(bidi, src, srcLen, UBIDI_RTL, NULL, &errorCode);
    u16on8bidi_setLine(bidi, 0, 6, bidiLine, &errorCode);
    destLen = u16on8bidi_getResultLength(bidiLine);
	REQUIRE(destLen == 5);
    REQUIRE(U_SUCCESS(errorCode));

    srcLen = u_unescape("abcdefghi", src, MAXLEN);
    u16on8bidi_setPara(bidi, src, srcLen, UBIDI_LTR, NULL, &errorCode);
    u16on8bidi_setLine(bidi, 0, 6, bidiLine, &errorCode);
    dir = u16on8bidi_getDirection(bidiLine);
	REQUIRE(dir == UBIDI_LTR);
    REQUIRE(U_SUCCESS(errorCode));

    u16on8bidi_setPara(bidi, src, 0, UBIDI_LTR, NULL, &errorCode);
    runCount = u16on8bidi_countRuns(bidi, &errorCode);
	REQUIRE(runCount == 0);
    REQUIRE(U_SUCCESS(errorCode));

    srcLen = u_unescape("          ", src, MAXLEN);
    u16on8bidi_setPara(bidi, src, srcLen, UBIDI_RTL, NULL, &errorCode);
    u16on8bidi_setLine(bidi, 0, 6, bidiLine, &errorCode);
    runCount = u16on8bidi_countRuns(bidiLine, &errorCode);
	REQUIRE(runCount == 1);
    REQUIRE(U_SUCCESS(errorCode));

    srcLen = u_unescape("a\\u05d0        bc", src, MAXLEN);
    u16on8bidi_setPara(bidi, src, srcLen, UBIDI_RTL, NULL, &errorCode);
    u16on8bidi_setLine(bidi, 0, 6, bidiLine, &errorCode);
    dir = u16on8bidi_getDirection(bidi);
	REQUIRE(dir == UBIDI_MIXED);
    dir = u16on8bidi_getDirection(bidiLine);
	REQUIRE(dir == UBIDI_MIXED);
    runCount = u16on8bidi_countRuns(bidiLine, &errorCode);
	REQUIRE(runCount == 2);
    REQUIRE(U_SUCCESS(errorCode));

    ubidi_invertMap(srcMap, map, 5);
	REQUIRE(memcmp(dstMap, map, sizeof(dstMap)) == 0);

    /* test REMOVE_BIDI_CONTROLS together with DO_MIRRORING */
    srcLen = u_unescape("abc\\u200e", src, MAXLEN);
    u16on8bidi_setPara(bidi, src, srcLen, UBIDI_LTR, NULL, &errorCode);
    destLen = u16on8bidi_writeReordered(bidi, dest, MAXLEN,
              UBIDI_REMOVE_BIDI_CONTROLS | UBIDI_DO_MIRRORING, &errorCode);
	REQUIRE(destLen == 3);
	REQUIRE(memcmp(dest, src, 3 * sizeof(UChar)) == 0);
    REQUIRE(U_SUCCESS(errorCode));

    /* test inverse Bidi with marks and contextual orientation */
    u16on8bidi_setReorderingMode(bidi, UBIDI_REORDER_INVERSE_LIKE_DIRECT);
    u16on8bidi_setReorderingOptions(bidi, UBIDI_OPTION_INSERT_MARKS);
    u16on8bidi_setPara(bidi, src, 0, UBIDI_DEFAULT_RTL, NULL, &errorCode);
    destLen = u16on8bidi_writeReordered(bidi, dest, MAXLEN, 0, &errorCode);
	REQUIRE(destLen == 0);
    REQUIRE(U_SUCCESS(errorCode));
    srcLen = u_unescape("   ", src, MAXLEN);
    u16on8bidi_setPara(bidi, src, srcLen, UBIDI_DEFAULT_RTL, NULL, &errorCode);
    destLen = u16on8bidi_writeReordered(bidi, dest, MAXLEN, 0, &errorCode);
	REQUIRE(destLen == 3);
	REQUIRE(memcmp(dest, src, destLen * sizeof(UChar)) == 0);
    REQUIRE(U_SUCCESS(errorCode));
    srcLen = u_unescape("abc", src, MAXLEN);
    u16on8bidi_setPara(bidi, src, srcLen, UBIDI_DEFAULT_RTL, NULL, &errorCode);
    destLen = u16on8bidi_writeReordered(bidi, dest, MAXLEN, 0, &errorCode);
	REQUIRE(destLen == 3);
	REQUIRE(memcmp(dest, src, destLen * sizeof(UChar)) == 0);
    REQUIRE(U_SUCCESS(errorCode));
    srcLen = u_unescape("\\u05d0\\u05d1", src, MAXLEN);
    u16on8bidi_setPara(bidi, src, srcLen, UBIDI_DEFAULT_RTL, NULL, &errorCode);
    destLen = u16on8bidi_writeReordered(bidi, dest, MAXLEN, 0, &errorCode);
    srcLen = u_unescape("\\u05d1\\u05d0", src, MAXLEN);
	REQUIRE(destLen == 2);
	REQUIRE(memcmp(dest, src, destLen * sizeof(UChar)) == 0);
    REQUIRE(U_SUCCESS(errorCode));
    srcLen = u_unescape("abc \\u05d0\\u05d1", src, MAXLEN);
    u16on8bidi_setPara(bidi, src, srcLen, UBIDI_DEFAULT_RTL, NULL, &errorCode);
    destLen = u16on8bidi_writeReordered(bidi, dest, MAXLEN, 0, &errorCode);
    srcLen = u_unescape("\\u05d1\\u05d0 abc", src, MAXLEN);
	REQUIRE(destLen == 6);
	REQUIRE(memcmp(dest, src, destLen * sizeof(UChar)) == 0);
    REQUIRE(U_SUCCESS(errorCode));
    srcLen = u_unescape("\\u05d0\\u05d1 abc", src, MAXLEN);
    u16on8bidi_setPara(bidi, src, srcLen, UBIDI_DEFAULT_RTL, NULL, &errorCode);
    destLen = u16on8bidi_writeReordered(bidi, dest, MAXLEN, 0, &errorCode);
    srcLen = u_unescape("\\u200fabc \\u05d1\\u05d0", src, MAXLEN);
	REQUIRE(destLen == 7);
	REQUIRE(memcmp(dest, src, destLen * sizeof(UChar)) == 0);
    REQUIRE(U_SUCCESS(errorCode));
    srcLen = u_unescape("\\u05d0\\u05d1 abc .-=", src, MAXLEN);
    u16on8bidi_setPara(bidi, src, srcLen, UBIDI_DEFAULT_RTL, NULL, &errorCode);
    destLen = u16on8bidi_writeReordered(bidi, dest, MAXLEN, 0, &errorCode);
    srcLen = u_unescape("\\u200f=-. abc \\u05d1\\u05d0", src, MAXLEN);
	REQUIRE(destLen == 11);
	REQUIRE(memcmp(dest, src, destLen * sizeof(UChar)) == 0);
    REQUIRE(U_SUCCESS(errorCode));
    u16on8bidi_orderParagraphsLTR(bidi, true);
    srcLen = u_unescape("\n\r   \n\rabc\n\\u05d0\\u05d1\rabc \\u05d2\\u05d3\n\r"
                        "\\u05d4\\u05d5 abc\n\\u05d6\\u05d7 abc .-=\r\n"
                        "-* \\u05d8\\u05d9 abc .-=", src, MAXLEN);
    u16on8bidi_setPara(bidi, src, srcLen, UBIDI_DEFAULT_RTL, NULL, &errorCode);
    destLen = u16on8bidi_writeReordered(bidi, dest, MAXLEN, 0, &errorCode);
    srcLen = u_unescape("\n\r   \n\rabc\n\\u05d1\\u05d0\r\\u05d3\\u05d2 abc\n\r"
                        "\\u200fabc \\u05d5\\u05d4\n\\u200f=-. abc \\u05d7\\u05d6\r\n"
                        "\\u200f=-. abc \\u05d9\\u05d8 *-", src, MAXLEN);
	REQUIRE(destLen == 57);
	REQUIRE(memcmp(dest, src, destLen * sizeof(UChar)) == 0);
    REQUIRE(U_SUCCESS(errorCode));
    srcLen = u_unescape("\\u05d0 \t", src, MAXLEN);
    u16on8bidi_setPara(bidi, src, srcLen, UBIDI_LTR, NULL, &errorCode);
    destLen = u16on8bidi_writeReordered(bidi, dest, MAXLEN, 0, &errorCode);
    srcLen = u_unescape("\\u05D0\\u200e \t", src, MAXLEN);
	REQUIRE(destLen == 4);
	REQUIRE(memcmp(dest, src, destLen * sizeof(UChar)) == 0);
    REQUIRE(U_SUCCESS(errorCode));
    srcLen = u_unescape("\\u05d0 123 \t\\u05d1 123 \\u05d2", src, MAXLEN);
    u16on8bidi_setPara(bidi, src, srcLen, UBIDI_LTR, NULL, &errorCode);
    destLen = u16on8bidi_writeReordered(bidi, dest, MAXLEN, 0, &errorCode);
    srcLen = u_unescape("\\u05d0 \\u200e123\\u200e \t\\u05d2 123 \\u05d1", src, MAXLEN);
	REQUIRE(destLen == 16);
	REQUIRE(memcmp(dest, src, destLen * sizeof(UChar)) == 0);
    REQUIRE(U_SUCCESS(errorCode));
    srcLen = u_unescape("\\u05d0 123 \\u0660\\u0661 ab", src, MAXLEN);
    u16on8bidi_setPara(bidi, src, srcLen, UBIDI_LTR, NULL, &errorCode);
    destLen = u16on8bidi_writeReordered(bidi, dest, MAXLEN, 0, &errorCode);
    srcLen = u_unescape("\\u05d0 \\u200e123 \\u200e\\u0660\\u0661 ab", src, MAXLEN);
	REQUIRE(destLen == 13);
	REQUIRE(memcmp(dest, src, destLen * sizeof(UChar)) == 0);
    REQUIRE(U_SUCCESS(errorCode));
    srcLen = u_unescape("ab \t", src, MAXLEN);
    u16on8bidi_setPara(bidi, src, srcLen, UBIDI_RTL, NULL, &errorCode);
    destLen = u16on8bidi_writeReordered(bidi, dest, MAXLEN, 0, &errorCode);
    srcLen = u_unescape("\\u200f\t ab", src, MAXLEN);
	REQUIRE(destLen == 5);
	REQUIRE(memcmp(dest, src, destLen * sizeof(UChar)) == 0);
    REQUIRE(U_SUCCESS(errorCode));

    /* check exceeding para level */
    u16on8bidi_close(bidi);
    bidi = u16on8bidi_open();
    srcLen = u_unescape("A\\u202a\\u05d0\\u202aC\\u202c\\u05d1\\u202cE", src, MAXLEN);
    u16on8bidi_setPara(bidi, src, srcLen, UBIDI_MAX_EXPLICIT_LEVEL - 1, NULL, &errorCode);
    level = u16on8bidi_getLevelAt(bidi, 2);
	REQUIRE(level == UBIDI_MAX_EXPLICIT_LEVEL);
    REQUIRE(U_SUCCESS(errorCode));

    /* check 1-char runs with RUNS_ONLY */
    u16on8bidi_setReorderingMode(bidi, UBIDI_REORDER_RUNS_ONLY);
    srcLen = u_unescape("a \\u05d0 b \\u05d1 c \\u05d2 d ", src, MAXLEN);
    u16on8bidi_setPara(bidi, src, srcLen, UBIDI_LTR, NULL, &errorCode);
    runCount = u16on8bidi_countRuns(bidi, &errorCode);
	REQUIRE(runCount == 14);
    REQUIRE(U_SUCCESS(errorCode));

    u16on8bidi_close(bidi);
    u16on8bidi_close(bidiLine);
}

static void _testReordering(U16On8BiDi *pBiDi, int testNumber) {
    int32_t
        logicalMap1[MAXLEN], logicalMap2[MAXLEN], logicalMap3[MAXLEN],
        visualMap1[MAXLEN], visualMap2[MAXLEN], visualMap3[MAXLEN], visualMap4[MAXLEN];
    UErrorCode errorCode=U_ZERO_ERROR;
    const UBiDiLevel *levels;
    int32_t i, length=u16on8bidi_getLength(pBiDi),
               destLength=u16on8bidi_getResultLength(pBiDi);
    int32_t runCount, visualIndex, logicalStart, runLength;
    UBool odd;

    if (length<=0) {
        return;
    }

    /* get the logical and visual maps from the object */
    u16on8bidi_getLogicalMap(pBiDi, logicalMap1, &errorCode);
	REQUIRE(U_SUCCESS(errorCode));

    u16on8bidi_getVisualMap(pBiDi, visualMap1, &errorCode);
	REQUIRE(U_SUCCESS(errorCode));

    /* invert them both */
    ubidi_invertMap(logicalMap1, visualMap2, length);
    ubidi_invertMap(visualMap1, logicalMap2, destLength);

    /* get them from the levels array, too */
    levels=u16on8bidi_getLevels(pBiDi, &errorCode);
	REQUIRE(U_SUCCESS(errorCode));

    ubidi_reorderLogical(levels, length, logicalMap3);
    ubidi_reorderVisual(levels, length, visualMap3);

    /* get the visual map from the runs, too */
    runCount=u16on8bidi_countRuns(pBiDi, &errorCode);
	REQUIRE(U_SUCCESS(errorCode));

    visualIndex=0;
    for(i=0; i<runCount; ++i) {
        odd=(UBool)u16on8bidi_getVisualRun(pBiDi, i, &logicalStart, &runLength);
        if(UBIDI_LTR==odd) {
            do { /* LTR */
                visualMap4[visualIndex++]=logicalStart++;
            } while(--runLength>0);
        } else {
            logicalStart+=runLength;   /* logicalLimit */
            do { /* RTL */
                visualMap4[visualIndex++]=--logicalStart;
            } while(--runLength>0);
        }
    }

    /* check that the indexes are the same between these and u16on8bidi_getLogical/VisualIndex() */
    for(i=0; i<length; ++i) {
        REQUIRE(logicalMap1[i] == logicalMap2[i]);
        REQUIRE(logicalMap1[i] == logicalMap3[i]);
        REQUIRE(visualMap1[i] == visualMap2[i]);
        REQUIRE(visualMap1[i] == visualMap3[i]);
        REQUIRE(visualMap1[i] == visualMap4[i]);
        REQUIRE(logicalMap1[i] == u16on8bidi_getVisualIndex(pBiDi, i, &errorCode));
        REQUIRE(U_SUCCESS(errorCode));
        REQUIRE(visualMap1[i] == u16on8bidi_getLogicalIndex(pBiDi, i, &errorCode));
        REQUIRE(U_SUCCESS(errorCode));
    }
}

static void initCharFromDirProps() {
    static const UVersionInfo ucd401={ 4, 0, 1, 0 };
    static UVersionInfo ucdVersion={ 0, 0, 0, 0 };

    /* lazy initialization */
    if(ucdVersion[0]>0) {
        return;
    }

    u_getUnicodeVersion(ucdVersion);
    if(memcmp(ucdVersion, ucd401, sizeof(UVersionInfo))>=0) {
        /* Unicode 4.0.1 changes bidi classes for +-/ */
        charFromDirProp[U_EUROPEAN_NUMBER_SEPARATOR]=0x2b; /* change ES character from / to + */
    }
}

static UChar* getStringFromDirProps(const uint8_t *dirProps, int32_t length, UChar *buffer) {
    int32_t i;

    initCharFromDirProps();

    /* this part would have to be modified for UTF-x */
    for(i=0; i<length; ++i) {
        buffer[i]=charFromDirProp[dirProps[i]];
    }
    buffer[length]=0;
    return buffer;
}

