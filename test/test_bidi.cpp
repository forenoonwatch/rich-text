#include <catch2/catch_test_macros.hpp>

#include "bidi_test.hpp"

#include <unicode/ustring.h>
#include <unicode/ucnv.h>
#include <unicode/ushape.h>
#include <cstring.h>

#define MAXLEN      MAX_STRING_LENGTH

/* new BIDI API */
static void testReorderingMode(void);
static void testReorderRunsOnly(void);
static void testStreaming(void);
static void testClassOverride(void);
static const char* inverseBasic(UBiDi *pBiDi, const char *src, int32_t srcLen,
                                uint32_t option, UBiDiLevel level, char *result);
static void assertRoundTrip(UBiDi *pBiDi, int32_t tc, int32_t outIndex, const char *srcChars,
		const char *destChars, const UChar *dest, int32_t destLen, int mode, int option, UBiDiLevel level);
static void checkResultLength(UBiDi *pBiDi, const char *srcChars, const char *destChars, int32_t destLen,
		const char *mode, const char *option, UBiDiLevel level);
static void checkMaps(UBiDi *pBiDi, int32_t stringIndex, const char *src, const char *dest, const char *mode,
		const char* option, UBiDiLevel level, UBool forward);

static void verifyCallbackParams(UBiDiClassCallback* fn, const void* context, UBiDiClassCallback* expectedFn,
		const void* expectedContext, int32_t sizeOfContext);

static char *aescstrdup(const UChar* unichars,int32_t length);

/* helpers ------------------------------------------------------------------ */

static const char *levelString="...............................................................";

static void initCharFromDirProps();
static UChar* getStringFromDirProps(const uint8_t *dirProps, int32_t length, UChar *buffer);

static int pseudoToU16(const int length, const char * input, UChar * output);
static int u16ToPseudo(const int length, const UChar * input, char * output);
static void checkWhatYouCan(UBiDi *bidi, const char *srcChars, const char *dstChars);

static void doTests(UBiDi *pBiDi, UBiDi *pLine, UBool countRunsFirst);
static void doTest(UBiDi *pBiDi, int testNumber, const BiDiTestData *test, int32_t lineStart,
		UBool countRunsFirst);
static void doMisc();

static void _testReordering(UBiDi *pBiDi, int testNumber);
static void _testInverseBidi(UBiDi *pBiDi, const UChar *src, int32_t srcLength, UBiDiLevel direction,
		UErrorCode *pErrorCode);
static void _testManyInverseBidi(UBiDi *pBiDi, UBiDiLevel direction);
static void _testWriteReverse();
static void _testManyAddedPoints();
static void _testMisc();

/* Reordering Mode BiDi --------------------------------------------------------- */

static const UBiDiLevel paraLevels[] = { UBIDI_LTR, UBIDI_RTL };

static UBiDi* getBiDiObject() {
    UBiDi* pBiDi = ubidi_open();
	REQUIRE(pBiDi != nullptr);
    return pBiDi;
}

#define MAKE_ITEMS(val) val, #val

static const struct {
    UBiDiReorderingMode value;
    const char* description;
}
modes[] = {
    { MAKE_ITEMS(UBIDI_REORDER_GROUP_NUMBERS_WITH_R) },
    { MAKE_ITEMS(UBIDI_REORDER_INVERSE_LIKE_DIRECT) },
    { MAKE_ITEMS(UBIDI_REORDER_NUMBERS_SPECIAL) },
    { MAKE_ITEMS(UBIDI_REORDER_INVERSE_FOR_NUMBERS_SPECIAL) },
    { MAKE_ITEMS(UBIDI_REORDER_INVERSE_NUMBERS_AS_L) }
};
static const struct {
    uint32_t value;
    const char* description;
}
options[] = {
    { MAKE_ITEMS(UBIDI_OPTION_INSERT_MARKS) },
    { MAKE_ITEMS(0) }
};

#define TC_COUNT                UPRV_LENGTHOF(textIn)
#define MODES_COUNT             UPRV_LENGTHOF(modes)
#define OPTIONS_COUNT           UPRV_LENGTHOF(options)
#define LEVELS_COUNT            UPRV_LENGTHOF(paraLevels)

static const char* const textIn[] = {
/* (0) 123 */
    "123",
/* (1) .123->4.5 */
    ".123->4.5",
/* (2) 678 */
    "678",
/* (3) .678->8.9 */
    ".678->8.9",
/* (4) JIH1.2,3MLK */
    "JIH1.2,3MLK",
/* (5) FE.>12-> */
    "FE.>12->",
/* (6) JIH.>12->a */
    "JIH.>12->a",
/* (7) CBA.>67->89=a */
    "CBA.>67->89=a",
/* (8) CBA.123->xyz */
    "CBA.123->xyz",
/* (9) .>12->xyz */
    ".>12->xyz",
/* (10) a.>67->xyz */
    "a.>67->xyz",
/* (11) 123JIH */
    "123JIH",
/* (12) 123 JIH */
    "123 JIH"
};

static const char* const textOut[] = {
/* TC 0: 123 */
    "123",                                                              /* (0) */
/* TC 1: .123->4.5 */
    ".123->4.5",                                                        /* (1) */
    "4.5<-123.",                                                        /* (2) */
/* TC 2: 678 */
    "678",                                                              /* (3) */
/* TC 3: .678->8.9 */
    ".8.9<-678",                                                        /* (4) */
    "8.9<-678.",                                                        /* (5) */
    ".678->8.9",                                                        /* (6) */
/* TC 4: MLK1.2,3JIH */
    "KLM1.2,3HIJ",                                                      /* (7) */
/* TC 5: FE.>12-> */
    "12<.EF->",                                                         /* (8) */
    "<-12<.EF",                                                         /* (9) */
    "EF.>@12->",                                                        /* (10) */
/* TC 6: JIH.>12->a */
    "12<.HIJ->a",                                                       /* (11) */
    "a<-12<.HIJ",                                                       /* (12) */
    "HIJ.>@12->a",                                                      /* (13) */
    "a&<-12<.HIJ",                                                      /* (14) */
/* TC 7: CBA.>67->89=a */
    "ABC.>@67->89=a",                                                   /* (15) */
    "a=89<-67<.ABC",                                                    /* (16) */
    "a&=89<-67<.ABC",                                                   /* (17) */
    "89<-67<.ABC=a",                                                    /* (18) */
/* TC 8: CBA.123->xyz */
    "123.ABC->xyz",                                                     /* (19) */
    "xyz<-123.ABC",                                                     /* (20) */
    "ABC.@123->xyz",                                                    /* (21) */
    "xyz&<-123.ABC",                                                    /* (22) */
/* TC 9: .>12->xyz */
    ".>12->xyz",                                                        /* (23) */
    "xyz<-12<.",                                                        /* (24) */
    "xyz&<-12<.",                                                       /* (25) */
/* TC 10: a.>67->xyz */
    "a.>67->xyz",                                                       /* (26) */
    "a.>@67@->xyz",                                                     /* (27) */
    "xyz<-67<.a",                                                       /* (28) */
/* TC 11: 123JIH */
    "123HIJ",                                                           /* (29) */
    "HIJ123",                                                           /* (30) */
/* TC 12: 123 JIH */
    "123 HIJ",                                                          /* (31) */
    "HIJ 123",                                                          /* (32) */
};

/* new BIDI API */

#define NO                  UBIDI_MAP_NOWHERE
#define MAX_MAP_LENGTH      20

static const int32_t forwardMap[][MAX_MAP_LENGTH] = {
/* TC 0: 123 */
    { 0, 1, 2 },                                                        /* (0) */
/* TC 1: .123->4.5 */
    { 0, 1, 2, 3, 4, 5, 6, 7, 8 },                                      /* (1) */
    { 8, 5, 6, 7, 4, 3, 0, 1, 2 },                                      /* (2) */
/* TC 2: 678 */
    { 0, 1, 2 },                                                        /* (3) */
/* TC 3: .678->8.9 */
    { 0, 6, 7, 8, 5, 4, 1, 2, 3 },                                      /* (4) */
    { 8, 5, 6, 7, 4, 3, 0, 1, 2 },                                      /* (5) */
    { 0, 1, 2, 3, 4, 5, 6, 7, 8 },                                      /* (6) */
/* TC 4: MLK1.2,3JIH */
    { 10, 9, 8, 3, 4, 5, 6, 7, 2, 1, 0 },                               /* (7) */
/* TC 5: FE.>12-> */
    { 5, 4, 3, 2, 0, 1, 6, 7 },                                         /* (8) */
    { 7, 6, 5, 4, 2, 3, 1, 0 },                                         /* (9) */
    { 1, 0, 2, 3, 5, 6, 7, 8 },                                         /* (10) */
/* TC 6: JIH.>12->a */
    { 6, 5, 4, 3, 2, 0, 1, 7, 8, 9 },                                   /* (11) */
    { 9, 8, 7, 6, 5, 3, 4, 2, 1, 0 },                                   /* (12) */
    { 2, 1, 0, 3, 4, 6, 7, 8, 9, 10 },                                  /* (13) */
    { 10, 9, 8, 7, 6, 4, 5, 3, 2, 0 },                                  /* (14) */
/* TC 7: CBA.>67->89=a */
    { 2, 1, 0, 3, 4, 6, 7, 8, 9, 10, 11, 12, 13 },                      /* (15) */
    { 12, 11, 10, 9, 8, 6, 7, 5, 4, 2, 3, 1, 0 },                       /* (16) */
    { 13, 12, 11, 10, 9, 7, 8, 6, 5, 3, 4, 2, 0 },                      /* (17) */
    { 10, 9, 8, 7, 6, 4, 5, 3, 2, 0, 1, 11, 12 },                       /* (18) */
/* TC 8: CBA.123->xyz */
    { 6, 5, 4, 3, 0, 1, 2, 7, 8, 9, 10, 11 },                           /* (19) */
    { 11, 10, 9, 8, 5, 6, 7, 4, 3, 0, 1, 2 },                           /* (20) */
    { 2, 1, 0, 3, 5, 6, 7, 8, 9, 10, 11, 12 },                          /* (21) */
    { 12, 11, 10, 9, 6, 7, 8, 5, 4, 0, 1, 2 },                          /* (22) */
/* TC 9: .>12->xyz */
    { 0, 1, 2, 3, 4, 5, 6, 7, 8 },                                      /* (23) */
    { 8, 7, 5, 6, 4, 3, 0, 1, 2 },                                      /* (24) */
    { 9, 8, 6, 7, 5, 4, 0, 1, 2 },                                      /* (25) */
/* TC 10: a.>67->xyz */
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 },                                   /* (26) */
    { 0, 1, 2, 4, 5, 7, 8, 9, 10, 11 },                                 /* (27) */
    { 9, 8, 7, 5, 6, 4, 3, 0, 1, 2 },                                   /* (28) */
/* TC 11: 123JIH */
    { 0, 1, 2, 5, 4, 3 },                                               /* (29) */
    { 3, 4, 5, 2, 1, 0 },                                               /* (30) */
/* TC 12: 123 JIH */
    { 0, 1, 2, 3, 6, 5, 4 },                                            /* (31) */
    { 4, 5, 6, 3, 2, 1, 0 },                                            /* (32) */
};

static const int32_t inverseMap[][MAX_MAP_LENGTH] = {
/* TC 0: 123 */
    { 0, 1, 2 },                                                        /* (0) */
/* TC 1: .123->4.5 */
    { 0, 1, 2, 3, 4, 5, 6, 7, 8 },                                      /* (1) */
    { 6, 7, 8, 5, 4, 1, 2, 3, 0 },                                      /* (2) */
/* TC 2: 678 */
    { 0, 1, 2 },                                                        /* (3) */
/* TC 3: .678->8.9 */
    { 0, 6, 7, 8, 5, 4, 1, 2, 3 },                                      /* (4) */
    { 6, 7, 8, 5, 4, 1, 2, 3, 0 },                                      /* (5) */
    { 0, 1, 2, 3, 4, 5, 6, 7, 8 },                                      /* (6) */
/* TC 4: MLK1.2,3JIH */
    { 10, 9, 8, 3, 4, 5, 6, 7, 2, 1, 0 },                               /* (7) */
/* TC 5: FE.>12-> */
    { 4, 5, 3, 2, 1, 0, 6, 7 },                                         /* (8) */
    { 7, 6, 4, 5, 3, 2, 1, 0 },                                         /* (9) */
    { 1, 0, 2, 3, NO, 4, 5, 6, 7 },                                     /* (10) */
/* TC 6: JIH.>12->a */
    { 5, 6, 4, 3, 2, 1, 0, 7, 8, 9 },                                   /* (11) */
    { 9, 8, 7, 5, 6, 4, 3, 2, 1, 0 },                                   /* (12) */
    { 2, 1, 0, 3, 4, NO, 5, 6, 7, 8, 9 },                               /* (13) */
    { 9, NO, 8, 7, 5, 6, 4, 3, 2, 1, 0 },                               /* (14) */
/* TC 7: CBA.>67->89=a */
    { 2, 1, 0, 3, 4, NO, 5, 6, 7, 8, 9, 10, 11, 12 },                   /* (15) */
    { 12, 11, 9, 10, 8, 7, 5, 6, 4, 3, 2, 1, 0 },                       /* (16) */
    { 12, NO, 11, 9, 10, 8, 7, 5, 6, 4, 3, 2, 1, 0 },                   /* (17) */
    { 9, 10, 8, 7, 5, 6, 4, 3, 2, 1, 0, 11, 12 },                       /* (18) */
/* TC 8: CBA.123->xyz */
    { 4, 5, 6, 3, 2, 1, 0, 7, 8, 9, 10, 11 },                           /* (19) */
    { 9, 10, 11, 8, 7, 4, 5, 6, 3, 2, 1, 0 },                           /* (20) */
    { 2, 1, 0, 3, NO, 4, 5, 6, 7, 8, 9, 10, 11 },                       /* (21) */
    { 9, 10, 11, NO, 8, 7, 4, 5, 6, 3, 2, 1, 0 },                       /* (22) */
/* TC 9: .>12->xyz */
    { 0, 1, 2, 3, 4, 5, 6, 7, 8 },                                      /* (23) */
    { 6, 7, 8, 5, 4, 2, 3, 1, 0 },                                      /* (24) */
    { 6, 7, 8, NO, 5, 4, 2, 3, 1, 0 },                                  /* (25) */
/* TC 10: a.>67->xyz */
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 },                                   /* (26) */
    { 0, 1, 2, NO, 3, 4, NO, 5, 6, 7, 8, 9 },                           /* (27) */
    { 7, 8, 9, 6, 5, 3, 4, 2, 1, 0 },                                   /* (28) */
/* TC 11: 123JIH */
    { 0, 1, 2, 5, 4, 3 },                                               /* (29) */
    { 5, 4, 3, 0, 1, 2 },                                               /* (30) */
/* TC 12: 123 JIH */
    { 0, 1, 2, 3, 6, 5, 4 },                                            /* (31) */
    { 6, 5, 4, 3, 0, 1, 2 },                                            /* (32) */
};

static const char outIndices[TC_COUNT][MODES_COUNT - 1][OPTIONS_COUNT]
            [LEVELS_COUNT] = {
    { /* TC 0: 123 */
        {{ 0,  0}, { 0,  0}}, /* UBIDI_REORDER_GROUP_NUMBERS_WITH_R */
        {{ 0,  0}, { 0,  0}}, /* UBIDI_REORDER_INVERSE_LIKE_DIRECT */
        {{ 0,  0}, { 0,  0}}, /* UBIDI_REORDER_NUMBERS_SPECIAL */
        {{ 0,  0}, { 0,  0}}  /* UBIDI_REORDER_INVERSE_FOR_NUMBERS_SPECIAL */
    },
    { /* TC 1: .123->4.5 */
        {{ 1,  2}, { 1,  2}}, /* UBIDI_REORDER_GROUP_NUMBERS_WITH_R */
        {{ 1,  2}, { 1,  2}}, /* UBIDI_REORDER_INVERSE_LIKE_DIRECT */
        {{ 1,  2}, { 1,  2}}, /* UBIDI_REORDER_NUMBERS_SPECIAL */
        {{ 1,  2}, { 1,  2}}  /* UBIDI_REORDER_INVERSE_FOR_NUMBERS_SPECIAL */
    },
    { /* TC 2: 678 */
        {{ 3,  3}, { 3,  3}}, /* UBIDI_REORDER_GROUP_NUMBERS_WITH_R */
        {{ 3,  3}, { 3,  3}}, /* UBIDI_REORDER_INVERSE_LIKE_DIRECT */
        {{ 3,  3}, { 3,  3}}, /* UBIDI_REORDER_NUMBERS_SPECIAL */
        {{ 3,  3}, { 3,  3}}  /* UBIDI_REORDER_INVERSE_FOR_NUMBERS_SPECIAL */
    },
    { /* TC 3: .678->8.9 */
        {{ 6,  5}, { 6,  5}}, /* UBIDI_REORDER_GROUP_NUMBERS_WITH_R */
        {{ 4,  5}, { 4,  5}}, /* UBIDI_REORDER_INVERSE_LIKE_DIRECT */
        {{ 6,  5}, { 6,  5}}, /* UBIDI_REORDER_NUMBERS_SPECIAL */
        {{ 6,  5}, { 6,  5}}  /* UBIDI_REORDER_INVERSE_FOR_NUMBERS_SPECIAL */
    },
    { /* TC 4: MLK1.2,3JIH */
        {{ 7,  7}, { 7,  7}}, /* UBIDI_REORDER_GROUP_NUMBERS_WITH_R */
        {{ 7,  7}, { 7,  7}}, /* UBIDI_REORDER_INVERSE_LIKE_DIRECT */
        {{ 7,  7}, { 7,  7}}, /* UBIDI_REORDER_NUMBERS_SPECIAL */
        {{ 7,  7}, { 7,  7}}  /* UBIDI_REORDER_INVERSE_FOR_NUMBERS_SPECIAL */
    },
    { /* TC 5: FE.>12-> */
        {{ 8,  9}, { 8,  9}}, /* UBIDI_REORDER_GROUP_NUMBERS_WITH_R */
        {{10,  9}, { 8,  9}}, /* UBIDI_REORDER_INVERSE_LIKE_DIRECT */
        {{ 8,  9}, { 8,  9}}, /* UBIDI_REORDER_NUMBERS_SPECIAL */
        {{10,  9}, { 8,  9}}  /* UBIDI_REORDER_INVERSE_FOR_NUMBERS_SPECIAL */
    },
    { /* TC 6: JIH.>12->a */
        {{11, 12}, {11, 12}}, /* UBIDI_REORDER_GROUP_NUMBERS_WITH_R */
        {{13, 14}, {11, 12}}, /* UBIDI_REORDER_INVERSE_LIKE_DIRECT */
        {{11, 12}, {11, 12}}, /* UBIDI_REORDER_NUMBERS_SPECIAL */
        {{13, 14}, {11, 12}}  /* UBIDI_REORDER_INVERSE_FOR_NUMBERS_SPECIAL */
    },
    { /* TC 7: CBA.>67->89=a */
        {{18, 16}, {18, 16}}, /* UBIDI_REORDER_GROUP_NUMBERS_WITH_R */
        {{18, 17}, {18, 16}}, /* UBIDI_REORDER_INVERSE_LIKE_DIRECT */
        {{18, 16}, {18, 16}}, /* UBIDI_REORDER_NUMBERS_SPECIAL */
        {{15, 17}, {18, 16}}  /* UBIDI_REORDER_INVERSE_FOR_NUMBERS_SPECIAL */
    },
    { /* TC 8: CBA.>124->xyz */
        {{19, 20}, {19, 20}}, /* UBIDI_REORDER_GROUP_NUMBERS_WITH_R */
        {{21, 22}, {19, 20}}, /* UBIDI_REORDER_INVERSE_LIKE_DIRECT */
        {{19, 20}, {19, 20}}, /* UBIDI_REORDER_NUMBERS_SPECIAL */
        {{21, 22}, {19, 20}}  /* UBIDI_REORDER_INVERSE_FOR_NUMBERS_SPECIAL */
    },
    { /* TC 9: .>12->xyz */
        {{23, 24}, {23, 24}}, /* UBIDI_REORDER_GROUP_NUMBERS_WITH_R */
        {{23, 25}, {23, 24}}, /* UBIDI_REORDER_INVERSE_LIKE_DIRECT */
        {{23, 24}, {23, 24}}, /* UBIDI_REORDER_NUMBERS_SPECIAL */
        {{23, 25}, {23, 24}}  /* UBIDI_REORDER_INVERSE_FOR_NUMBERS_SPECIAL */
    },
    { /* TC 10: a.>67->xyz */
        {{26, 26}, {26, 26}}, /* UBIDI_REORDER_GROUP_NUMBERS_WITH_R */
        {{26, 27}, {26, 28}}, /* UBIDI_REORDER_INVERSE_LIKE_DIRECT */
        {{26, 28}, {26, 28}}, /* UBIDI_REORDER_NUMBERS_SPECIAL */
        {{26, 27}, {26, 28}}  /* UBIDI_REORDER_INVERSE_FOR_NUMBERS_SPECIAL */
    },
    { /* TC 11: 124JIH */
        {{30, 30}, {30, 30}}, /* UBIDI_REORDER_GROUP_NUMBERS_WITH_R */
        {{29, 30}, {29, 30}}, /* UBIDI_REORDER_INVERSE_LIKE_DIRECT */
        {{30, 30}, {30, 30}}, /* UBIDI_REORDER_NUMBERS_SPECIAL */
        {{30, 30}, {30, 30}}  /* UBIDI_REORDER_INVERSE_FOR_NUMBERS_SPECIAL */
    },
    { /* TC 12: 124 JIH */
        {{32, 32}, {32, 32}}, /* UBIDI_REORDER_GROUP_NUMBERS_WITH_R */
        {{31, 32}, {31, 32}}, /* UBIDI_REORDER_INVERSE_LIKE_DIRECT */
        {{31, 32}, {31, 32}}, /* UBIDI_REORDER_NUMBERS_SPECIAL */
        {{31, 32}, {31, 32}}  /* UBIDI_REORDER_INVERSE_FOR_NUMBERS_SPECIAL */
    }
};

typedef struct {
    const char* prologue;
    const char* source;
    const char* epilogue;
    const char* expected;
    UBiDiLevel paraLevel;
} contextCase;

static const contextCase contextData[] = {
    /*00*/  {"", "", "", "", UBIDI_LTR},
    /*01*/  {"", ".-=JKL-+*", "", ".-=LKJ-+*", UBIDI_LTR},
    /*02*/  {" ", ".-=JKL-+*", " ", ".-=LKJ-+*", UBIDI_LTR},
    /*03*/  {"a", ".-=JKL-+*", "b", ".-=LKJ-+*", UBIDI_LTR},
    /*04*/  {"D", ".-=JKL-+*", "", "LKJ=-.-+*", UBIDI_LTR},
    /*05*/  {"", ".-=JKL-+*", " D", ".-=*+-LKJ", UBIDI_LTR},
    /*06*/  {"", ".-=JKL-+*", " 2", ".-=*+-LKJ", UBIDI_LTR},
    /*07*/  {"", ".-=JKL-+*", " 7", ".-=*+-LKJ", UBIDI_LTR},
    /*08*/  {" G 1", ".-=JKL-+*", " H", "*+-LKJ=-.", UBIDI_LTR},
    /*09*/  {"7", ".-=JKL-+*", " H", ".-=*+-LKJ", UBIDI_LTR},
    /*10*/  {"", ".-=abc-+*", "", "*+-abc=-.", UBIDI_RTL},
    /*11*/  {" ", ".-=abc-+*", " ", "*+-abc=-.", UBIDI_RTL},
    /*12*/  {"D", ".-=abc-+*", "G", "*+-abc=-.", UBIDI_RTL},
    /*13*/  {"x", ".-=abc-+*", "", "*+-.-=abc", UBIDI_RTL},
    /*14*/  {"", ".-=abc-+*", " y", "abc-+*=-.", UBIDI_RTL},
    /*15*/  {"", ".-=abc-+*", " 2", "abc-+*=-.", UBIDI_RTL},
    /*16*/  {" x 1", ".-=abc-+*", " 2", ".-=abc-+*", UBIDI_RTL},
    /*17*/  {" x 7", ".-=abc-+*", " 8", "*+-.-=abc", UBIDI_RTL},
    /*18*/  {"x|", ".-=abc-+*", " 8", "*+-abc=-.", UBIDI_RTL},
    /*19*/  {"G|y", ".-=abc-+*", " 8", "*+-.-=abc", UBIDI_RTL},
    /*20*/  {"", ".-=", "", ".-=", UBIDI_DEFAULT_LTR},
    /*21*/  {"D", ".-=", "", "=-.", UBIDI_DEFAULT_LTR},
    /*22*/  {"G", ".-=", "", "=-.", UBIDI_DEFAULT_LTR},
    /*23*/  {"xG", ".-=", "", ".-=", UBIDI_DEFAULT_LTR},
    /*24*/  {"x|G", ".-=", "", "=-.", UBIDI_DEFAULT_LTR},
    /*25*/  {"x|G", ".-=|-+*", "", "=-.|-+*", UBIDI_DEFAULT_LTR},
};
#define CONTEXT_COUNT       UPRV_LENGTHOF(contextData)

// Tests

TEST_CASE("TestCharFromDirProp", "[BiDi]") {
	/* verify that the exemplar characters have the expected bidi classes */
	initCharFromDirProps();

	for (int32_t i = 0; i < U_CHAR_DIRECTION_COUNT; ++i) {
		CHECK(u_charDirection(charFromDirProp[i]) == (UCharDirection)i);
	}
}

TEST_CASE("TestBiDi", "[BiDi]") {
	UBiDi *pBiDi=NULL, *pLine=NULL;
    UErrorCode errorCode=U_ZERO_ERROR;

    pBiDi = ubidi_openSized(MAXLEN, 0, &errorCode);
	REQUIRE(pBiDi != nullptr);
	pLine = ubidi_open();
	REQUIRE(pLine != nullptr);

	doTests(pBiDi, pLine, false);
	doTests(pBiDi, pLine, true);
    doMisc();

    if (pLine!=NULL) {
        ubidi_close(pLine);
    }
    if (pBiDi!=NULL) {
        ubidi_close(pBiDi);
    }
}

static int countRoundtrips=0, countNonRoundtrips=0;

#define STRING_TEST_CASE(s) { (s), UPRV_LENGTHOF(s) }

TEST_CASE("TestInverse", "[BiDi]") {
	static const UChar
        string0[]={ 0x6c, 0x61, 0x28, 0x74, 0x69, 0x6e, 0x20, 0x5d0, 0x5d1, 0x29, 0x5d2, 0x5d3 },
        string1[]={ 0x6c, 0x61, 0x74, 0x20, 0x5d0, 0x5d1, 0x5d2, 0x20, 0x31, 0x32, 0x33 },
        string2[]={ 0x6c, 0x61, 0x74, 0x20, 0x5d0, 0x28, 0x5d1, 0x5d2, 0x20, 0x31, 0x29, 0x32, 0x33 },
        string3[]={ 0x31, 0x32, 0x33, 0x20, 0x5d0, 0x5d1, 0x5d2, 0x20, 0x34, 0x35, 0x36 },
        string4[]={ 0x61, 0x62, 0x20, 0x61, 0x62, 0x20, 0x661, 0x662 };

    static const struct {
        const UChar *s;
        int32_t length;
    } testCases[]={
        STRING_TEST_CASE(string0),
        STRING_TEST_CASE(string1),
        STRING_TEST_CASE(string2),
        STRING_TEST_CASE(string3),
        STRING_TEST_CASE(string4)
    };

    UBiDi *pBiDi;
    UErrorCode errorCode;
    int i;

    pBiDi=ubidi_open();
	REQUIRE(pBiDi != nullptr);

     for(i=0; i<UPRV_LENGTHOF(testCases); ++i) {
        errorCode=U_ZERO_ERROR;
        _testInverseBidi(pBiDi, testCases[i].s, testCases[i].length, 0, &errorCode);
    }

    for(i=0; i<UPRV_LENGTHOF(testCases); ++i) {
        errorCode=U_ZERO_ERROR;
        _testInverseBidi(pBiDi, testCases[i].s, testCases[i].length, 1, &errorCode);
    }

    _testManyInverseBidi(pBiDi, 0);
    _testManyInverseBidi(pBiDi, 1);

    ubidi_close(pBiDi);

    _testWriteReverse();
    _testManyAddedPoints();
    _testMisc();
}

TEST_CASE("TestReorder", "[BiDi]") {
	static const char* const logicalOrder[] ={
            "del(KC)add(K.C.&)",
            "del(QDVT) add(BVDL)",
            "del(PQ)add(R.S.)T)U.&",
            "del(LV)add(L.V.) L.V.&",
            "day  0  R  DPDHRVR dayabbr",
            "day  1  H  DPHPDHDA dayabbr",
            "day  2   L  DPBLENDA dayabbr",
            "day  3  J  DPJQVM  dayabbr",
            "day  4   I  DPIQNF    dayabbr",
            "day  5  M  DPMEG  dayabbr",
            "helloDPMEG",
            "hello WXY"
    };
    static const char* const visualOrder[]={
            "del(CK)add(&.C.K)",
            "del(TVDQ) add(LDVB)",
            "del(QP)add(S.R.)&.U(T",            /* updated for Unicode 6.3 matching brackets */
            "del(VL)add(V.L.) &.V.L",           /* updated for Unicode 6.3 matching brackets */
            "day  0  RVRHDPD  R dayabbr",
            "day  1  ADHDPHPD  H dayabbr",
            "day  2   ADNELBPD  L dayabbr",
            "day  3  MVQJPD  J  dayabbr",
            "day  4   FNQIPD  I    dayabbr",
            "day  5  GEMPD  M  dayabbr",
            "helloGEMPD",
            "hello YXW"
    };
    static const char* const visualOrder1[]={
            ")K.C.&(dda)KC(led",
            ")BVDL(dda )QDVT(led",
            "T(U.&).R.S(dda)PQ(led",            /* updated for Unicode 6.3 matching brackets */
            "L.V.& ).L.V(dda)LV(led",           /* updated for Unicode 6.3 matching brackets */
            "rbbayad R  DPDHRVR  0  yad",
            "rbbayad H  DPHPDHDA  1  yad",
            "rbbayad L  DPBLENDA   2  yad",
            "rbbayad  J  DPJQVM  3  yad",
            "rbbayad    I  DPIQNF   4  yad",
            "rbbayad  M  DPMEG  5  yad",
            "DPMEGolleh",
            "WXY olleh"
    };

    static const char* const visualOrder2[]={
            "@)@K.C.&@(dda)@KC@(led",
            "@)@BVDL@(dda )@QDVT@(led",
            "R.S.)T)U.&@(dda)@PQ@(led",
            "L.V.) L.V.&@(dda)@LV@(led",
            "rbbayad @R  DPDHRVR@  0  yad",
            "rbbayad @H  DPHPDHDA@  1  yad",
            "rbbayad @L  DPBLENDA@   2  yad",
            "rbbayad  @J  DPJQVM@  3  yad",
            "rbbayad    @I  DPIQNF@   4  yad",
            "rbbayad  @M  DPMEG@  5  yad",
            "DPMEGolleh",
            "WXY@ olleh"
    };
    static const char* const visualOrder3[]={
            ")K.C.&(KC)dda(led",
            ")BVDL(ddaQDVT) (led",
            "R.S.)T)U.&(PQ)dda(led",
            "L.V.) L.V.&(LV)dda(led",
            "rbbayad DPDHRVR   R  0 yad",
            "rbbayad DPHPDHDA   H  1 yad",
            "rbbayad DPBLENDA     L 2 yad",
            "rbbayad  DPJQVM   J  3 yad",
            "rbbayad    DPIQNF     I 4 yad",
            "rbbayad  DPMEG   M  5 yad",
            "DPMEGolleh",
            "WXY olleh"
    };
    static const char* const visualOrder4[]={
            "del(add(CK(.C.K)",
            "del( (TVDQadd(LDVB)",
            "del(add(QP(.U(T(.S.R",
            "del(add(VL(.V.L (.V.L",
            "day 0  R   RVRHDPD dayabbr",
            "day 1  H   ADHDPHPD dayabbr",
            "day 2 L     ADNELBPD dayabbr",
            "day 3  J   MVQJPD  dayabbr",
            "day 4 I     FNQIPD    dayabbr",
            "day 5  M   GEMPD  dayabbr",
            "helloGEMPD",
            "hello YXW"
    };
    char formatChars[MAXLEN];
    UErrorCode ec = U_ZERO_ERROR;
    UBiDi* bidi = ubidi_open();
    int i;

    for(i=0;i<UPRV_LENGTHOF(logicalOrder);i++){
        int32_t srcSize = (int32_t)strlen(logicalOrder[i]);
        int32_t destSize = srcSize*2;
        UChar src[MAXLEN];
        UChar dest[MAXLEN];
        char chars[MAXLEN];
        pseudoToU16(srcSize,logicalOrder[i],src);
        ec = U_ZERO_ERROR;
        ubidi_setPara(bidi,src,srcSize,UBIDI_DEFAULT_LTR ,NULL,&ec);
		REQUIRE(U_SUCCESS(ec));
        /* try pre-flighting */
        destSize = ubidi_writeReordered(bidi,dest,0,UBIDI_DO_MIRRORING,&ec);
		REQUIRE(ec == U_BUFFER_OVERFLOW_ERROR);
		REQUIRE(destSize == srcSize);
        ec = U_ZERO_ERROR;
        destSize=ubidi_writeReordered(bidi,dest,destSize+1,UBIDI_DO_MIRRORING,&ec);
        u16ToPseudo(destSize,dest,chars);
		REQUIRE(destSize == srcSize);
		REQUIRE(strcmp(visualOrder[i],chars) == 0);
        checkWhatYouCan(bidi, logicalOrder[i], chars);
    }

    for(i=0;i<UPRV_LENGTHOF(logicalOrder);i++){
        int32_t srcSize = (int32_t)strlen(logicalOrder[i]);
        int32_t destSize = srcSize*2;
        UChar src[MAXLEN];
        UChar dest[MAXLEN];
        char chars[MAXLEN];
        pseudoToU16(srcSize,logicalOrder[i],src);
        ec = U_ZERO_ERROR;
        ubidi_setPara(bidi,src,srcSize,UBIDI_DEFAULT_LTR ,NULL,&ec);
		REQUIRE(U_SUCCESS(ec));
        /* try pre-flighting */
        destSize = ubidi_writeReordered(bidi,dest,0,UBIDI_DO_MIRRORING+UBIDI_OUTPUT_REVERSE,&ec);
		REQUIRE(ec == U_BUFFER_OVERFLOW_ERROR);
		REQUIRE(destSize == srcSize);
		ec = U_ZERO_ERROR;
        destSize=ubidi_writeReordered(bidi,dest,destSize+1,UBIDI_DO_MIRRORING+UBIDI_OUTPUT_REVERSE,&ec);
        u16ToPseudo(destSize,dest,chars);
		REQUIRE(destSize == srcSize);
		REQUIRE(strcmp(visualOrder1[i],chars) == 0);
    }

    for(i=0;i<UPRV_LENGTHOF(logicalOrder);i++){
        int32_t srcSize = (int32_t)strlen(logicalOrder[i]);
        int32_t destSize = srcSize*2;
        UChar src[MAXLEN];
        UChar dest[MAXLEN];
        char chars[MAXLEN];
        pseudoToU16(srcSize,logicalOrder[i],src);
        ec = U_ZERO_ERROR;
        ubidi_setInverse(bidi,true);
        ubidi_setPara(bidi,src,srcSize,UBIDI_DEFAULT_LTR ,NULL,&ec);
		REQUIRE(U_SUCCESS(ec));
                /* try pre-flighting */
        destSize = ubidi_writeReordered(bidi,dest,0,UBIDI_INSERT_LRM_FOR_NUMERIC+UBIDI_OUTPUT_REVERSE,&ec);
		REQUIRE(ec == U_BUFFER_OVERFLOW_ERROR);
		ec = U_ZERO_ERROR;
        destSize=ubidi_writeReordered(bidi,dest,destSize+1,UBIDI_INSERT_LRM_FOR_NUMERIC+UBIDI_OUTPUT_REVERSE,&ec);
        u16ToPseudo(destSize,dest,chars);
		REQUIRE(strcmp(visualOrder2[i],chars) == 0);
    }
        /* Max Explicit level */
    for(i=0;i<UPRV_LENGTHOF(logicalOrder);i++){
        int32_t srcSize = (int32_t)strlen(logicalOrder[i]);
        int32_t destSize = srcSize*2;
        UChar src[MAXLEN];
        UChar dest[MAXLEN];
        char chars[MAXLEN];
        UBiDiLevel levels[UBIDI_MAX_EXPLICIT_LEVEL]={1,2,3,4,5,6,7,8,9,10};
        pseudoToU16(srcSize,logicalOrder[i],src);
        ec = U_ZERO_ERROR;
        ubidi_setPara(bidi,src,srcSize,UBIDI_DEFAULT_LTR,levels,&ec);
		REQUIRE(U_SUCCESS(ec));
                /* try pre-flighting */
        destSize = ubidi_writeReordered(bidi,dest,0,UBIDI_OUTPUT_REVERSE,&ec);
		REQUIRE(ec == U_BUFFER_OVERFLOW_ERROR);
		REQUIRE(destSize == srcSize);
        ec = U_ZERO_ERROR;
        destSize=ubidi_writeReordered(bidi,dest,destSize+1,UBIDI_OUTPUT_REVERSE,&ec);
        u16ToPseudo(destSize,dest,chars);
		REQUIRE(destSize == srcSize);
        REQUIRE(strcmp(visualOrder3[i],chars) == 0);
    }
    for(i=0;i<UPRV_LENGTHOF(logicalOrder);i++){
        int32_t srcSize = (int32_t)strlen(logicalOrder[i]);
        int32_t destSize = srcSize*2;
        UChar src[MAXLEN];
        UChar dest[MAXLEN];
        char chars[MAXLEN];
        UBiDiLevel levels[UBIDI_MAX_EXPLICIT_LEVEL]={1,2,3,4,5,6,7,8,9,10};
        pseudoToU16(srcSize,logicalOrder[i],src);
        ec = U_ZERO_ERROR;
        ubidi_setPara(bidi,src,srcSize,UBIDI_DEFAULT_LTR,levels,&ec);
		REQUIRE(U_SUCCESS(ec));
        /* try pre-flighting */
        destSize = ubidi_writeReordered(bidi,dest,0,UBIDI_DO_MIRRORING+UBIDI_REMOVE_BIDI_CONTROLS,&ec);
		REQUIRE(ec == U_BUFFER_OVERFLOW_ERROR);
        ec = U_ZERO_ERROR;
        destSize=ubidi_writeReordered(bidi,dest,destSize+1,UBIDI_DO_MIRRORING+UBIDI_REMOVE_BIDI_CONTROLS,&ec);
        u16ToPseudo(destSize,dest,chars);
        REQUIRE(strcmp(visualOrder4[i],chars) == 0);
    }
    ubidi_close(bidi);
}

TEST_CASE("TestFailureRecovery", "[BiDi]") {
	UErrorCode errorCode;
    UBiDi *bidi, *bidiLine;
    UChar src[MAXLEN];
    int32_t srcLen;
    UBiDiLevel level;
    UBiDiReorderingMode rm;
    static UBiDiLevel myLevels[3] = {6,5,4};

    errorCode = U_FILE_ACCESS_ERROR;
    REQUIRE(ubidi_writeReordered(NULL, NULL, 0, 0, &errorCode) == 0);
    REQUIRE(ubidi_writeReverse(NULL, 0, NULL, 0, 0, &errorCode) == 0);
    errorCode = U_ZERO_ERROR;
    REQUIRE(ubidi_writeReordered(NULL, NULL, 0, 0, &errorCode) == 0);
	REQUIRE(errorCode == U_ILLEGAL_ARGUMENT_ERROR);

    bidi = ubidi_open();
    srcLen = u_unescape("abc", src, MAXLEN);
    errorCode = U_ZERO_ERROR;
    ubidi_setPara(bidi, src, srcLen, UBIDI_DEFAULT_LTR - 1, NULL, &errorCode);
	REQUIRE(U_FAILURE(errorCode));
    errorCode = U_ZERO_ERROR;
	REQUIRE(ubidi_writeReverse(NULL, 0, NULL, 0, 0, &errorCode) == 0);
	REQUIRE(errorCode == U_ILLEGAL_ARGUMENT_ERROR);
    bidiLine = ubidi_open();
    errorCode = U_ZERO_ERROR;
    ubidi_setLine(bidi, 0, 6, bidiLine, &errorCode);
	REQUIRE(U_FAILURE(errorCode));
    errorCode = U_ZERO_ERROR;
    srcLen = u_unescape("abc", src, MAXLEN);
    ubidi_setPara(bidi, src, srcLen, UBIDI_LTR + 4, NULL, &errorCode);
    level = ubidi_getLevelAt(bidi, 3);
	REQUIRE(level == 0);
    errorCode = U_ZERO_ERROR;
    ubidi_close(bidi);
    bidi = ubidi_openSized(-1, 0, &errorCode);
	REQUIRE(U_FAILURE(errorCode));
    ubidi_close(bidi);
    bidi = ubidi_openSized(2, 1, &errorCode);
    errorCode = U_ZERO_ERROR;
    srcLen = u_unescape("abc", src, MAXLEN);
    ubidi_setPara(bidi, src, srcLen, UBIDI_LTR, NULL, &errorCode);
	REQUIRE(U_FAILURE(errorCode));
    errorCode = U_ZERO_ERROR;
    srcLen = u_unescape("=2", src, MAXLEN);
    ubidi_setPara(bidi, src, srcLen, UBIDI_RTL, NULL, &errorCode);
    ubidi_countRuns(bidi, &errorCode);
	REQUIRE(U_FAILURE(errorCode));
    ubidi_close(bidi);
    bidi = ubidi_open();
    rm = ubidi_getReorderingMode(bidi);
    ubidi_setReorderingMode(bidi, (UBiDiReorderingMode)(UBIDI_REORDER_DEFAULT - 1));
    REQUIRE(rm == ubidi_getReorderingMode(bidi));
    ubidi_setReorderingMode(bidi, (UBiDiReorderingMode)9999);
    REQUIRE(rm == ubidi_getReorderingMode(bidi));

    /* Try a surrogate char */
    errorCode = U_ZERO_ERROR;
    srcLen = u_unescape("\\uD800\\uDC00", src, MAXLEN);
    ubidi_setPara(bidi, src, srcLen, UBIDI_RTL, NULL, &errorCode);
    REQUIRE(ubidi_getDirection(bidi) == UBIDI_MIXED);
    errorCode = U_ZERO_ERROR;
    srcLen = u_unescape("abc", src, MAXLEN);
    ubidi_setPara(bidi, src, srcLen, 5, myLevels, &errorCode);
	REQUIRE(U_FAILURE(errorCode));
    ubidi_close(bidi);
    ubidi_close(bidiLine);
}

TEST_CASE("TestMultipleParagraphs", "[BiDi]") {
	static const char* const text = "__ABC\\u001c"          /* Para #0 offset 0 */
                                    "__\\u05d0DE\\u001c"    /*       1        6 */
                                    "__123\\u001c"          /*       2       12 */
                                    "\\u000d\\u000a"        /*       3       18 */
                                    "FG\\u000d"             /*       4       20 */
                                    "\\u000d"               /*       5       23 */
                                    "HI\\u000d\\u000a"      /*       6       24 */
                                    "\\u000d\\u000a"        /*       7       28 */
                                    "\\u000a"               /*       8       30 */
                                    "\\u000a"               /*       9       31 */
                                    "JK\\u001c";            /*      10       32 */
    static const int32_t paraCount=11;
    static const int32_t paraBounds[]={0, 6, 12, 18, 20, 23, 24, 28, 30, 31, 32, 35};
    static const UBiDiLevel paraLevels[]={UBIDI_LTR, UBIDI_RTL, UBIDI_DEFAULT_LTR, UBIDI_DEFAULT_RTL, 22, 23};
    static const UBiDiLevel multiLevels[6][11] = {{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
                                                  {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
                                                  {0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0},
                                                  {0, 1, 1, 1, 0, 1, 0, 1, 1, 1, 0},
                                                  {22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22},
                                                  {23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23}};
    static const char* const text2 = "\\u05d0 1-2\\u001c\\u0630 1-2\\u001c1-2";
    static const UBiDiLevel levels2[] = {1,1,2,2,2,0, 1,1,2,1,2,0, 2,2,2};
    static UBiDiLevel myLevels[10] = {0,0,0,0,0,0,0,0,0,0};
    static const UChar multiparaTestString[] = {
        0x5de, 0x5e0, 0x5e1, 0x5d4, 0x20,  0x5e1, 0x5e4, 0x5da,
        0x20,  0xa,   0xa,   0x41,  0x72,  0x74,  0x69,  0x73,
        0x74,  0x3a,  0x20,  0x5de, 0x5e0, 0x5e1, 0x5d4, 0x20,
        0x5e1, 0x5e4, 0x5da, 0x20,  0xa,   0xa,   0x41,  0x6c,
        0x62,  0x75,  0x6d,  0x3a,  0x20,  0x5de, 0x5e0, 0x5e1,
        0x5d4, 0x20,  0x5e1, 0x5e4, 0x5da, 0x20,  0xa,   0xa,
        0x54,  0x69,  0x6d,  0x65,  0x3a,  0x20,  0x32,  0x3a,
        0x32,  0x37,  0xa,  0xa
    };
    static const UBiDiLevel multiparaTestLevels[] = {
        1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 1, 1, 1, 1, 1,
        1, 1, 1, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 1, 1, 1,
        1, 1, 1, 1, 1, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0
    };
    UBiDiLevel gotLevel;
    const UBiDiLevel* gotLevels;
    UBool orderParagraphsLTR;
    UChar src[MAXLEN], dest[MAXLEN];
    UErrorCode errorCode=U_ZERO_ERROR;
    UBiDi* pBidi=ubidi_open();
    UBiDi* pLine;
    int32_t srcSize, count, paraStart, paraLimit, paraIndex, length;
    int32_t srcLen, destLen;
    int i, j, k;

    u_unescape(text, src, MAXLEN);
    srcSize=u_strlen(src);
    ubidi_setPara(pBidi, src, srcSize, UBIDI_LTR, NULL, &errorCode);
	REQUIRE(U_SUCCESS(errorCode));
    /* check paragraph count and boundaries */
	REQUIRE(paraCount == (count = ubidi_countParagraphs(pBidi)));
    for (i=0; i<paraCount; i++) {
        ubidi_getParagraphByIndex(pBidi, i, &paraStart, &paraLimit, NULL, &errorCode);
		REQUIRE(paraStart == paraBounds[i]);
		REQUIRE(paraLimit == paraBounds[i+1]);
    }
    errorCode=U_ZERO_ERROR;
    /* check with last paragraph not terminated by B */
    src[srcSize-1]='L';
    ubidi_setPara(pBidi, src, srcSize, UBIDI_LTR, NULL, &errorCode);
	REQUIRE(U_SUCCESS(errorCode));
	REQUIRE(paraCount == (count = ubidi_countParagraphs(pBidi)));
    i=paraCount-1;
    ubidi_getParagraphByIndex(pBidi, i, &paraStart, &paraLimit, NULL, &errorCode);
	REQUIRE(paraStart == paraBounds[i]);
	REQUIRE(paraLimit == paraBounds[i+1]);
    errorCode=U_ZERO_ERROR;
    /* check paraLevel for all paragraphs under various paraLevel specs */
    for (k=0; k<6; k++) {
        ubidi_setPara(pBidi, src, srcSize, paraLevels[k], NULL, &errorCode);
        for (i=0; i<paraCount; i++) {
            paraIndex=ubidi_getParagraph(pBidi, paraBounds[i], NULL, NULL, &gotLevel, &errorCode);
			REQUIRE(paraIndex == i);
			REQUIRE(gotLevel == multiLevels[k][i]);
        }
        gotLevel=ubidi_getParaLevel(pBidi);
		REQUIRE(gotLevel == multiLevels[k][0]);
    }
    errorCode=U_ZERO_ERROR;
    /* check that the result of ubidi_getParaLevel changes if the first
     * paragraph has a different level
     */
    src[0]=0x05d2;                      /* Hebrew letter Gimel */
    ubidi_setPara(pBidi, src, srcSize, UBIDI_DEFAULT_LTR, NULL, &errorCode);
    gotLevel=ubidi_getParaLevel(pBidi);
	REQUIRE(gotLevel == UBIDI_RTL);
    errorCode=U_ZERO_ERROR;
    /* check that line cannot overlap paragraph boundaries */
    pLine=ubidi_open();
    i=paraBounds[1];
    k=paraBounds[2]+1;
    ubidi_setLine(pBidi, i, k, pLine, &errorCode);
	REQUIRE(U_FAILURE(errorCode));
    errorCode=U_ZERO_ERROR;
    i=paraBounds[1];
    k=paraBounds[2];
    ubidi_setLine(pBidi, i, k, pLine, &errorCode);
	REQUIRE(U_SUCCESS(errorCode));
    /* check level of block separator at end of paragraph when orderParagraphsLTR==false */
    ubidi_setPara(pBidi, src, srcSize, UBIDI_RTL, NULL, &errorCode);
    /* get levels through para Bidi block */
    gotLevels=ubidi_getLevels(pBidi, &errorCode);
	REQUIRE(U_SUCCESS(errorCode));
    for (i=26; i<32; i++) {
        REQUIRE(gotLevels[i] == UBIDI_RTL);
    }
    /* get levels through para Line block */
    i=paraBounds[1];
    k=paraBounds[2];
    ubidi_setLine(pBidi, i, k, pLine, &errorCode);
	REQUIRE(U_SUCCESS(errorCode));
    paraIndex=ubidi_getParagraph(pLine, i, &paraStart, &paraLimit, &gotLevel, &errorCode);
    gotLevels=ubidi_getLevels(pLine, &errorCode);
	REQUIRE(U_SUCCESS(errorCode));
    length=ubidi_getLength(pLine);
	REQUIRE(gotLevel == UBIDI_RTL);
	REQUIRE(gotLevels[length-1] == UBIDI_RTL);
    orderParagraphsLTR=ubidi_isOrderParagraphsLTR(pBidi);
	REQUIRE(!orderParagraphsLTR);
    ubidi_orderParagraphsLTR(pBidi, true);
    orderParagraphsLTR=ubidi_isOrderParagraphsLTR(pBidi);
	REQUIRE(orderParagraphsLTR);
    /* check level of block separator at end of paragraph when orderParagraphsLTR==true */
    ubidi_setPara(pBidi, src, srcSize, UBIDI_RTL, NULL, &errorCode);
    /* get levels through para Bidi block */
    gotLevels=ubidi_getLevels(pBidi, &errorCode);
    for (i=26; i<32; i++) {
		REQUIRE(gotLevels[i] == 0);
    }
    errorCode=U_ZERO_ERROR;
    /* get levels through para Line block */
    i=paraBounds[1];
    k=paraBounds[2];
    ubidi_setLine(pBidi, paraStart, paraLimit, pLine, &errorCode);
    paraIndex=ubidi_getParagraph(pLine, i, &paraStart, &paraLimit, &gotLevel, &errorCode);
    gotLevels=ubidi_getLevels(pLine, &errorCode);
    length=ubidi_getLength(pLine);
	REQUIRE(gotLevel == UBIDI_RTL);
	REQUIRE(gotLevels[length-1] == 0);

    /* test that the concatenation of separate invocations of the bidi code
     * on each individual paragraph in order matches the levels array that
     * results from invoking bidi once over the entire multiparagraph tests
     * (with orderParagraphsLTR false, of course)
     */
    u_unescape(text, src, MAXLEN);      /* restore original content */
    srcSize=u_strlen(src);
    ubidi_orderParagraphsLTR(pBidi, false);
    ubidi_setPara(pBidi, src, srcSize, UBIDI_DEFAULT_RTL, NULL, &errorCode);
    gotLevels=ubidi_getLevels(pBidi, &errorCode);
    for (i=0; i<paraCount; i++) {
        /* use pLine for individual paragraphs */
        paraStart = paraBounds[i];
        length = paraBounds[i+1] - paraStart;
        ubidi_setPara(pLine, src+paraStart, length, UBIDI_DEFAULT_RTL, NULL, &errorCode);
        for (j=0; j<length; j++) {
			REQUIRE((k=ubidi_getLevelAt(pLine, j)) == (gotLevel=gotLevels[paraStart+j]));
        }
    }

    /* ensure that leading numerics in a paragraph are not treated as arabic
       numerals because of arabic text in a preceding paragraph
     */
    u_unescape(text2, src, MAXLEN);
    srcSize=u_strlen(src);
    ubidi_orderParagraphsLTR(pBidi, true);
    ubidi_setPara(pBidi, src, srcSize, UBIDI_RTL, NULL, &errorCode);
    gotLevels=ubidi_getLevels(pBidi, &errorCode);
	REQUIRE(U_SUCCESS(errorCode));
    for (i=0; i<srcSize; i++) {
		REQUIRE(gotLevels[i] == levels2[i]);
    }

    /* check handling of whitespace before end of paragraph separator when
     * orderParagraphsLTR==true, when last paragraph has, and lacks, a terminating B
     */
    u_memset(src, 0x0020, MAXLEN);
    srcSize = 5;
    ubidi_orderParagraphsLTR(pBidi, true);
    for (i=0x001c; i<=0x0020; i+=(0x0020-0x001c)) {
        src[4]=(UChar)i;                /* with and without terminating B */
        for (j=0x0041; j<=0x05d0; j+=(0x05d0-0x0041)) {
            src[0]=(UChar)j;            /* leading 'A' or Alef */
            for (gotLevel=4; gotLevel<=5; gotLevel++) {
                /* test even and odd paraLevel */
                ubidi_setPara(pBidi, src, srcSize, gotLevel, NULL, &errorCode);
                gotLevels=ubidi_getLevels(pBidi, &errorCode);
                for (k=1; k<=3; k++) {
					REQUIRE(gotLevels[k] == gotLevel);
                }
            }
        }
    }

    /* check default orientation when inverse bidi and paragraph starts
     * with LTR strong char and ends with RTL strong char, with and without
     * a terminating B
     */
    ubidi_setReorderingMode(pBidi, UBIDI_REORDER_INVERSE_LIKE_DIRECT);
    srcLen = u_unescape("abc \\u05d2\\u05d1\n", src, MAXLEN);
    ubidi_setPara(pBidi, src, srcLen, UBIDI_DEFAULT_LTR, NULL, &errorCode);
    destLen = ubidi_writeReordered(pBidi, dest, MAXLEN, 0, &errorCode);
    srcLen = u_unescape("\\u05d1\\u05d2 abc\n", src, MAXLEN);
	REQUIRE(memcmp(src, dest, destLen * sizeof(UChar)) == 0);
    srcLen = u_unescape("abc \\u05d2\\u05d1", src, MAXLEN);
    ubidi_setPara(pBidi, src, srcLen, UBIDI_DEFAULT_LTR, NULL, &errorCode);
    destLen = ubidi_writeReordered(pBidi, dest, MAXLEN, 0, &errorCode);
    srcLen = u_unescape("\\u05d1\\u05d2 abc", src, MAXLEN);
	REQUIRE(memcmp(src, dest, destLen * sizeof(UChar)) == 0);

    /* check multiple paragraphs together with explicit levels
     */
    ubidi_setReorderingMode(pBidi, UBIDI_REORDER_DEFAULT);
    srcLen = u_unescape("ab\\u05d1\\u05d2\n\\u05d3\\u05d4123", src, MAXLEN);
    ubidi_setPara(pBidi, src, srcLen, UBIDI_LTR, myLevels, &errorCode);
    destLen = ubidi_writeReordered(pBidi, dest, MAXLEN, 0, &errorCode);
    srcLen = u_unescape("ab\\u05d2\\u05d1\\n123\\u05d4\\u05d3", src, MAXLEN);
	REQUIRE(memcmp(src, dest, destLen * sizeof(UChar)) == 0);
    count = ubidi_countParagraphs(pBidi);
	REQUIRE(count == 2);

    ubidi_close(pLine);
    ubidi_close(pBidi);

    /* check levels in multiple paragraphs with default para level
     */
    pBidi = ubidi_open();
    errorCode = U_ZERO_ERROR;
    ubidi_setPara(pBidi, multiparaTestString, UPRV_LENGTHOF(multiparaTestString),
                  UBIDI_DEFAULT_LTR, NULL, &errorCode);
	REQUIRE(U_SUCCESS(errorCode));
    gotLevels = ubidi_getLevels(pBidi, &errorCode);
	REQUIRE(U_SUCCESS(errorCode));
    for (i = 0; i < UPRV_LENGTHOF(multiparaTestString); i++) {
		REQUIRE(gotLevels[i] == multiparaTestLevels[i]);
    }
    ubidi_close(pBidi);
}

TEST_CASE("TestReorderingMode", "[BiDi]") {
	UChar src[MAXLEN], dest[MAXLEN];
    char destChars[MAXLEN];
    UBiDi *pBiDi = NULL, *pBiDi2 = NULL, *pBiDi3 = NULL;
    UErrorCode rc;
    int tc, mode, option, level;
    uint32_t optionValue, optionBack;
    UBiDiReorderingMode modeValue, modeBack;
    int32_t srcLen, destLen, idx;
    const char *expectedChars;

    pBiDi = getBiDiObject();
    pBiDi2 = getBiDiObject();
    pBiDi3 = getBiDiObject();
    if(!pBiDi3) {
        ubidi_close(pBiDi);             /* in case this one was allocated */
        ubidi_close(pBiDi2);            /* in case this one was allocated */
        return;
    }

    ubidi_setInverse(pBiDi2, true);

    for (tc = 0; tc < TC_COUNT; tc++) {
        const char *srcChars = textIn[tc];
        srcLen = (int32_t)strlen(srcChars);
        pseudoToU16(srcLen, srcChars, src);

        for (mode = 0; mode < MODES_COUNT; mode++) {
            modeValue = modes[mode].value;
            ubidi_setReorderingMode(pBiDi, modeValue);
            modeBack = ubidi_getReorderingMode(pBiDi);
			REQUIRE(modeValue == modeBack);

            for (option = 0; option < OPTIONS_COUNT; option++) {
                optionValue = options[option].value;
                ubidi_setReorderingOptions(pBiDi, optionValue);
                optionBack = ubidi_getReorderingOptions(pBiDi);
                REQUIRE(optionValue == optionBack);

                for (level = 0; level < LEVELS_COUNT; level++) {
                    rc = U_ZERO_ERROR;
                    ubidi_setPara(pBiDi, src, srcLen, paraLevels[level], NULL, &rc);
					REQUIRE(U_SUCCESS(rc));

                    *dest = 0;
                    destLen = ubidi_writeReordered(pBiDi, dest, MAXLEN,
                                                   UBIDI_DO_MIRRORING, &rc);
					REQUIRE(U_SUCCESS(rc));
                    u16ToPseudo(destLen, dest, destChars);
                    if (!((modes[mode].value == UBIDI_REORDER_INVERSE_NUMBERS_AS_L) &&
                          (options[option].value == UBIDI_OPTION_INSERT_MARKS))) {
                        checkWhatYouCan(pBiDi, srcChars, destChars);
                    }

                    if (modes[mode].value == UBIDI_REORDER_INVERSE_NUMBERS_AS_L) {
                        idx = -1;
                        expectedChars = inverseBasic(pBiDi2, srcChars, srcLen,
                                options[option].value, paraLevels[level], destChars);
                    }
                    else {
                        idx = outIndices[tc][mode][option][level];
                        expectedChars = textOut[idx];
                    }
					REQUIRE(uprv_strcmp(expectedChars, destChars) == 0);

					if (options[option].value == UBIDI_OPTION_INSERT_MARKS) {
						// FIXME: Why does this fail?
						if (tc != 5 && mode != 2 && option != 0 && level != 0) {
							assertRoundTrip(pBiDi3, tc, idx, srcChars, destChars, dest, destLen, mode, option,
									paraLevels[level]);
						}
					}
					checkResultLength(pBiDi, srcChars, destChars, destLen, modes[mode].description,
                                options[option].description, paraLevels[level]);
					if (idx > -1) {
						checkMaps(pBiDi, idx, srcChars, destChars, modes[mode].description,
								options[option].description, paraLevels[level], true);
					}
                }
            }
        }
    }
    ubidi_close(pBiDi3);
    ubidi_close(pBiDi2);
    ubidi_close(pBiDi);
}

TEST_CASE("TestReorderRunsOnly", "[BiDi]") {
	static const struct {
        const char* textIn;
        const char* textOut[2][2];
        const char noroundtrip[2];
    } testCases[] = {
        {"ab 234 896 de", {{"de 896 ab 234", "de 896 ab 234"},                   /*0*/
                           {"ab 234 @896@ de", "de 896 ab 234"}}, {0, 0}},
        {"abcGHI", {{"GHIabc", "GHIabc"}, {"GHIabc", "GHIabc"}}, {0, 0}},        /*1*/
        {"a.>67->", {{"<-67<.a", "<-67<.a"}, {"<-67<.a", "<-67<.a"}}, {0, 0}},   /*2*/
        {"-=%$123/ *", {{"* /%$123=-", "* /%$123=-"},                            /*3*/
                        {"* /%$123=-", "* /%$123=-"}}, {0, 0}},
        {"abc->12..>JKL", {{"JKL<..12<-abc", "JKL<..abc->12"},                   /*4*/
                           {"JKL<..12<-abc", "JKL<..abc->12"}}, {0, 0}},
        {"JKL->12..>abc", {{"abc<..JKL->12", "abc<..12<-JKL"},                   /*5*/
                           {"abc<..JKL->12", "abc<..12<-JKL"}}, {0, 0}},
        {"123->abc", {{"abc<-123", "abc<-123"},                                  /*6*/
                      {"abc&<-123", "abc<-123"}}, {1, 0}},
        {"123->JKL", {{"JKL<-123", "123->JKL"},                                  /*7*/
                      {"JKL<-123", "JKL<-@123"}}, {0, 1}},
        {"*>12.>34->JKL", {{"JKL<-34<.12<*", "12.>34->JKL<*"},                   /*8*/
                           {"JKL<-34<.12<*", "JKL<-@34<.12<*"}}, {0, 1}},
        {"*>67.>89->JKL", {{"67.>89->JKL<*", "67.>89->JKL<*"},                   /*9*/
                           {"67.>89->JKL<*", "67.>89->JKL<*"}}, {0, 0}},
        {"* /abc-=$%123", {{"$%123=-abc/ *", "abc-=$%123/ *"},                   /*10*/
                           {"$%123=-abc/ *", "abc-=$%123/ *"}}, {0, 0}},
        {"* /$%def-=123", {{"123=-def%$/ *", "def-=123%$/ *"},                   /*11*/
                           {"123=-def%$/ *", "def-=123%$/ *"}}, {0, 0}},
        {"-=GHI* /123%$", {{"GHI* /123%$=-", "123%$/ *GHI=-"},                   /*12*/
                           {"GHI* /123%$=-", "123%$/ *GHI=-"}}, {0, 0}},
        {"-=%$JKL* /123", {{"JKL* /%$123=-", "123/ *JKL$%=-"},                   /*13*/
                           {"JKL* /%$123=-", "123/ *JKL$%=-"}}, {0, 0}},
        {"ab =#CD *?450", {{"CD *?450#= ab", "450?* CD#= ab"},                   /*14*/
                           {"CD *?450#= ab", "450?* CD#= ab"}}, {0, 0}},
        {"ab 234 896 de", {{"de 896 ab 234", "de 896 ab 234"},                   /*15*/
                           {"ab 234 @896@ de", "de 896 ab 234"}}, {0, 0}},
        {"abc-=%$LMN* /123", {{"LMN* /%$123=-abc", "123/ *LMN$%=-abc"},          /*16*/
                              {"LMN* /%$123=-abc", "123/ *LMN$%=-abc"}}, {0, 0}},
        {"123->JKL&MN&P", {{"JKLMNP<-123", "123->JKLMNP"},                       /*17*/
                           {"JKLMNP<-123", "JKLMNP<-@123"}}, {0, 1}},
        {"123", {{"123", "123"},                /* just one run */               /*18*/
                 {"123", "123"}}, {0, 0}}
    };
    UBiDi *pBiDi = getBiDiObject();
    UBiDi *pL2VBiDi = getBiDiObject();
    UChar src[MAXLEN], dest[MAXLEN], visual1[MAXLEN], visual2[MAXLEN];
    char destChars[MAXLEN], vis1Chars[MAXLEN], vis2Chars[MAXLEN];
    int32_t srcLen, destLen, vis1Len, vis2Len, option, i, j, nCases, paras;
    UErrorCode rc = U_ZERO_ERROR;
    UBiDiLevel level;

    if(!pL2VBiDi) {
        ubidi_close(pBiDi);             /* in case this one was allocated */
        return;
    }
    ubidi_setReorderingMode(pBiDi, UBIDI_REORDER_RUNS_ONLY);
    ubidi_setReorderingOptions(pL2VBiDi, UBIDI_OPTION_REMOVE_CONTROLS);

    for (option = 0; option < 2; option++) {
        ubidi_setReorderingOptions(pBiDi, option==0 ? UBIDI_OPTION_REMOVE_CONTROLS
                                                    : UBIDI_OPTION_INSERT_MARKS);
        for (i = 0, nCases = UPRV_LENGTHOF(testCases); i < nCases; i++) {
            srcLen = (int32_t)strlen(testCases[i].textIn);
            pseudoToU16(srcLen, testCases[i].textIn, src);
            for(j = 0; j < 2; j++) {
                level = paraLevels[j];
                ubidi_setPara(pBiDi, src, srcLen, level, NULL, &rc);
				REQUIRE(U_SUCCESS(rc));
                *dest = 0;
                destLen = ubidi_writeReordered(pBiDi, dest, MAXLEN, UBIDI_DO_MIRRORING, &rc);
				REQUIRE(U_SUCCESS(rc));
                u16ToPseudo(destLen, dest, destChars);
                checkWhatYouCan(pBiDi, testCases[i].textIn, destChars);
				REQUIRE(uprv_strcmp(testCases[i].textOut[option][level], destChars) == 0);

                if((option==0) && testCases[i].noroundtrip[level]) {
                    continue;
                }
                ubidi_setPara(pL2VBiDi, src, srcLen, level, NULL, &rc);
				REQUIRE(U_SUCCESS(rc));
                *visual1 = 0;
                vis1Len = ubidi_writeReordered(pL2VBiDi, visual1, MAXLEN, UBIDI_DO_MIRRORING, &rc);
				REQUIRE(U_SUCCESS(rc));
                u16ToPseudo(vis1Len, visual1, vis1Chars);
                checkWhatYouCan(pL2VBiDi, testCases[i].textIn, vis1Chars);
                ubidi_setPara(pL2VBiDi, dest, destLen, level^1, NULL, &rc);
				REQUIRE(U_SUCCESS(rc));
                *visual2 = 0;
                vis2Len = ubidi_writeReordered(pL2VBiDi, visual2, MAXLEN, UBIDI_DO_MIRRORING, &rc);
				REQUIRE(U_SUCCESS(rc));
                u16ToPseudo(vis2Len, visual2, vis2Chars);
                checkWhatYouCan(pL2VBiDi, destChars, vis2Chars);
				REQUIRE(uprv_strcmp(vis1Chars, vis2Chars) == 0);
            }
        }
    }

    /* test with null or empty text */
    ubidi_setPara(pBiDi, src, 0, UBIDI_LTR, NULL, &rc);
	REQUIRE(U_SUCCESS(rc));
    paras = ubidi_countParagraphs(pBiDi);
	REQUIRE(paras == 0);

    ubidi_close(pBiDi);
    ubidi_close(pL2VBiDi);
}

TEST_CASE("TestStreaming", "[BiDi]") {
	#define MAXPORTIONS 10
	#define NULL_CHAR '\0'

    static const struct {
        const char* textIn;
        short int chunk;
        short int nPortions[2];
        char  portionLens[2][MAXPORTIONS];
        const char* message[2];
    } testData[] = {
        {   "123\\u000A"
            "abc45\\u000D"
            "67890\\u000A"
            "\\u000D"
            "02468\\u000D"
            "ghi",
            6, { 6, 6 }, {{ 4, 6, 6, 1, 6, 3}, { 4, 6, 6, 1, 6, 3 }},
            {"4, 6, 6, 1, 6, 3", "4, 6, 6, 1, 6, 3"}
        },
        {   "abcd\\u000Afgh\\u000D12345\\u000A456",
            6, { 4, 4 }, {{ 5, 4, 6, 3 }, { 5, 4, 6, 3 }},
            {"5, 4, 6, 3", "5, 4, 6, 3"}
        },
        {   "abcd\\u000Afgh\\u000D12345\\u000A45\\u000D",
            6, { 4, 4 }, {{ 5, 4, 6, 3 }, { 5, 4, 6, 3 }},
            {"5, 4, 6, 3", "5, 4, 6, 3"}
        },
        {   "abcde\\u000Afghi",
            10, { 2, 2 }, {{ 6, 4 }, { 6, 4 }},
            {"6, 4", "6, 4"}
        }
    };
    UChar src[MAXLEN];
    UBiDi *pBiDi = NULL;
    UChar *pSrc;
    UErrorCode rc = U_ZERO_ERROR;
    int32_t srcLen, processedLen, chunk, len, nPortions;
    int i, j, levelIndex;
    UBiDiLevel level;
    int nTests = UPRV_LENGTHOF(testData), nLevels = UPRV_LENGTHOF(paraLevels);
    UBool mismatch;
	char processedLenStr[MAXPORTIONS * 5];

    pBiDi = getBiDiObject();

    ubidi_orderParagraphsLTR(pBiDi, true);

    for (levelIndex = 0; levelIndex < nLevels; levelIndex++) {
        for (i = 0; i < nTests; i++) {
            srcLen = u_unescape(testData[i].textIn, src, MAXLEN);
            chunk = testData[i].chunk;
            nPortions = testData[i].nPortions[levelIndex];
            level = paraLevels[levelIndex];
            processedLenStr[0] = NULL_CHAR;

            mismatch = false;

            ubidi_setReorderingOptions(pBiDi, UBIDI_OPTION_STREAMING);
            for (j = 0, pSrc = src; j < MAXPORTIONS && srcLen > 0; j++) {

                len = chunk < srcLen ? chunk : srcLen;
                ubidi_setPara(pBiDi, pSrc, len, level, NULL, &rc);
				REQUIRE(U_SUCCESS(rc));

                processedLen = ubidi_getProcessedLength(pBiDi);
                if (processedLen == 0) {
                    ubidi_setReorderingOptions(pBiDi, UBIDI_OPTION_DEFAULT);
                    j--;
                    continue;
                }
                ubidi_setReorderingOptions(pBiDi, UBIDI_OPTION_STREAMING);

                mismatch |= (UBool)(j >= nPortions ||
                           processedLen != testData[i].portionLens[levelIndex][j]);

                snprintf(processedLenStr + j * 4, sizeof(processedLenStr) - j * 4, "%4d", processedLen);
                srcLen -= processedLen, pSrc += processedLen;
            }

			REQUIRE(!mismatch);
			REQUIRE(j == nPortions);
        }
    }
    ubidi_close(pBiDi);
}

U_CDECL_BEGIN static UCharDirection U_CALLCONV overrideBidiClass(const void *context, UChar32 c) {
#define DEF U_BIDI_CLASS_DEFAULT

    static const UCharDirection customClasses[] = {
       /* 0/8    1/9    2/A    3/B    4/C    5/D    6/E    7/F  */
          DEF,   DEF,   DEF,   DEF,   DEF,   DEF,   DEF,   DEF, /* 00-07 */
          DEF,   DEF,   DEF,   DEF,   DEF,   DEF,   DEF,   DEF, /* 08-0F */
          DEF,   DEF,   DEF,   DEF,   DEF,   DEF,   DEF,   DEF, /* 10-17 */
          DEF,   DEF,   DEF,   DEF,   DEF,   DEF,   DEF,   DEF, /* 18-1F */
          DEF,   DEF,   DEF,   DEF,   DEF,   DEF,     R,   DEF, /* 20-27 */
          DEF,   DEF,   DEF,   DEF,   DEF,   DEF,   DEF,   DEF, /* 28-2F */
           EN,    EN,    EN,    EN,    EN,    EN,    AN,    AN, /* 30-37 */
           AN,    AN,   DEF,   DEF,   DEF,   DEF,   DEF,   DEF, /* 38-3F */
            L,    AL,    AL,    AL,    AL,    AL,    AL,     R, /* 40-47 */
            R,     R,     R,     R,     R,     R,     R,     R, /* 48-4F */
            R,     R,     R,     R,     R,     R,     R,     R, /* 50-57 */
            R,     R,     R,   LRE,   DEF,   RLE,   PDF,     S, /* 58-5F */
          NSM,   DEF,   DEF,   DEF,   DEF,   DEF,   DEF,   DEF, /* 60-67 */
          DEF,   DEF,   DEF,   DEF,   DEF,   DEF,   DEF,   DEF, /* 68-6F */
          DEF,   DEF,   DEF,   DEF,   DEF,   DEF,   DEF,   DEF, /* 70-77 */
          DEF,   DEF,   DEF,   LRO,     B,   RLO,    BN,   DEF  /* 78-7F */
    };
    static const int nEntries = UPRV_LENGTHOF(customClasses);
    const char *dummy = (const char*)context;        /* just to avoid a compiler warning */
    dummy++;

    return c >= nEntries ? U_BIDI_CLASS_DEFAULT : customClasses[c];
}
U_CDECL_END

TEST_CASE("TestClassOverride", "[BiDi]") {
	static const char* const textSrc  = "JIH.>12->a \\u05D0\\u05D1 6 ABC78";
    static const char* const textResult = "12<.HIJ->a 78CBA 6 \\u05D1\\u05D0";

    UChar src[MAXLEN], dest[MAXLEN];
    UErrorCode rc = U_ZERO_ERROR;
    UBiDi *pBiDi = NULL;
    UBiDiClassCallback* oldFn = NULL;
    UBiDiClassCallback* newFn = overrideBidiClass;
    const void* oldContext = NULL;
    int32_t srcLen, destLen, textSrcSize = (int32_t)uprv_strlen(textSrc);
    char* destChars = NULL;

    pBiDi = getBiDiObject();
    if(!pBiDi) {
        return;
    }

    ubidi_getClassCallback(pBiDi, &oldFn, &oldContext);
    verifyCallbackParams(oldFn, oldContext, NULL, NULL, 0);

    ubidi_setClassCallback(pBiDi, newFn, textSrc, &oldFn, &oldContext, &rc);
	REQUIRE(U_SUCCESS(rc));
    // Quick callback test (API coverage).
	REQUIRE(ubidi_getCustomizedClass(pBiDi, u'A') == AL);
	REQUIRE(ubidi_getCustomizedClass(pBiDi, u'H') == R);
	REQUIRE(ubidi_getCustomizedClass(pBiDi, u'^') == PDF);
	REQUIRE(ubidi_getCustomizedClass(pBiDi, u'~') == BN);

    verifyCallbackParams(oldFn, oldContext, NULL, NULL, 0);

    ubidi_getClassCallback(pBiDi, &oldFn, &oldContext);
    verifyCallbackParams(oldFn, oldContext, newFn, textSrc, textSrcSize);

    ubidi_setClassCallback(pBiDi, newFn, textSrc, &oldFn, &oldContext, &rc);
	REQUIRE(U_SUCCESS(rc));
    verifyCallbackParams(oldFn, oldContext, newFn, textSrc, textSrcSize);

    srcLen = u_unescape(textSrc, src, MAXLEN);
    ubidi_setPara(pBiDi, src, srcLen, UBIDI_LTR, NULL, &rc);
	REQUIRE(U_SUCCESS(rc));

    destLen = ubidi_writeReordered(pBiDi, dest, MAXLEN,
                                   UBIDI_DO_MIRRORING, &rc);
	REQUIRE(U_SUCCESS(rc));

    destChars = aescstrdup(dest, destLen);
	REQUIRE(uprv_strcmp(textResult, destChars) == 0);
    ubidi_close(pBiDi);
}

TEST_CASE("TestGetBaseDirection", "[BiDi]") {
	UBiDiDirection dir;
    int i;

/* Test Data */
    static const UChar
/*Mixed Start with L*/
    stringMixedEnglishFirst[]={ 0x61, 0x627, 0x32, 0x6f3, 0x61, 0x34, 0 },
/*Mixed Start with AL*/
    stringMixedArabicFirst[]={ 0x661, 0x627, 0x662, 0x6f3, 0x61, 0x664, 0 },
/*Mixed Start with R*/
    stringMixedHebrewFirst[]={ 0x05EA, 0x627, 0x662, 0x6f3, 0x61, 0x664, 0 },
/*All AL (Arabic. Persian)*/
    stringPersian[]={0x0698, 0x067E, 0x0686, 0x06AF, 0},
/*All R (Hebrew etc.)*/
    stringHebrew[]={0x0590, 0x05D5, 0x05EA, 0x05F1, 0},
/*All L (English)*/
    stringEnglish[]={0x71, 0x61, 0x66, 0},
/*Mixed Start with weak AL an then L*/
    stringStartWeakAL[]={ 0x0663, 0x71, 0x61, 0x66, 0},
/*Mixed Start with weak L and then AL*/
    stringStartWeakL[]={0x31, 0x0698, 0x067E, 0x0686, 0x06AF, 0},
/*Empty*/
    stringEmpty[]={0},
/*Surrogate Char.*/
    stringSurrogateChar[]={0xD800, 0xDC00, 0},
/*Invalid UChar*/
    stringInvalidUchar[]={(UChar)-1},
/*All weak L (English Digits)*/
    stringAllEnglishDigits[]={0x31, 0x32, 0x33, 0},
/*All weak AL (Arabic Digits)*/
    stringAllArabicDigits[]={0x0663, 0x0664, 0x0665, 0},
/*First L (English) others are R (Hebrew etc.) */
    stringFirstL[] = {0x71, 0x0590, 0x05D5, 0x05EA, 0x05F1, 0},
/*Last R (Hebrew etc.) others are weak L (English Digits)*/
    stringLastR[] = {0x31, 0x32, 0x33, 0x05F1, 0};

    static const struct {
        const UChar *s;
        int32_t length;
    } testCases[]={
        STRING_TEST_CASE(stringMixedEnglishFirst),
        STRING_TEST_CASE(stringMixedArabicFirst),
        STRING_TEST_CASE(stringMixedHebrewFirst),
        STRING_TEST_CASE(stringPersian),
        STRING_TEST_CASE(stringHebrew),
        STRING_TEST_CASE(stringEnglish),
        STRING_TEST_CASE(stringStartWeakAL),
        STRING_TEST_CASE(stringStartWeakL),
        STRING_TEST_CASE(stringEmpty),
        STRING_TEST_CASE(stringSurrogateChar),
        STRING_TEST_CASE(stringInvalidUchar),
        STRING_TEST_CASE(stringAllEnglishDigits),
        STRING_TEST_CASE(stringAllArabicDigits),
        STRING_TEST_CASE(stringFirstL),
        STRING_TEST_CASE(stringLastR),
    };

	/* Expected results */
    static const UBiDiDirection expectedDir[] ={
        UBIDI_LTR, UBIDI_RTL, UBIDI_RTL,
        UBIDI_RTL, UBIDI_RTL, UBIDI_LTR,
        UBIDI_LTR, UBIDI_RTL, UBIDI_NEUTRAL,
        UBIDI_LTR, UBIDI_NEUTRAL, UBIDI_NEUTRAL,
        UBIDI_NEUTRAL, UBIDI_LTR, UBIDI_RTL
    };

	/* Run Tests */
	for(i=0; i<UPRV_LENGTHOF(testCases); ++i) {
        REQUIRE(ubidi_getBaseDirection(testCases[i].s, testCases[i].length) == expectedDir[i]);
    }

	/* Misc. tests */
	/* NULL string */
    REQUIRE(ubidi_getBaseDirection(NULL, 3) == UBIDI_NEUTRAL);
	/*All L- English string and length=-3 */
    REQUIRE(ubidi_getBaseDirection( stringEnglish, -3) == UBIDI_NEUTRAL);
	/*All L- English string and length=-1 */
    REQUIRE(ubidi_getBaseDirection( stringEnglish, -1) == UBIDI_LTR);
	/*All AL- Persian string and length=-1 */
    REQUIRE(ubidi_getBaseDirection( stringPersian, -1) == UBIDI_RTL);
	/*All R- Hebrew string and length=-1 */
    REQUIRE(ubidi_getBaseDirection( stringHebrew, -1) == UBIDI_RTL);
	/*All weak L- English digits string and length=-1 */
    REQUIRE(ubidi_getBaseDirection(stringAllEnglishDigits, -1) == UBIDI_NEUTRAL);
	/*All weak AL- Arabic digits string and length=-1 */
    REQUIRE(ubidi_getBaseDirection(stringAllArabicDigits, -1) == UBIDI_NEUTRAL);
}

TEST_CASE("TestContext", "[BiDi]") {
	UChar prologue[MAXLEN], epilogue[MAXLEN], src[MAXLEN], dest[MAXLEN];
    char destChars[MAXLEN];
    UBiDi *pBiDi = NULL;
    UErrorCode rc;
    int32_t proLength, epiLength, srcLen, destLen, tc;
    contextCase cc;

    /* test null BiDi object */
    rc = U_ZERO_ERROR;
    ubidi_setContext(pBiDi, NULL, 0, NULL, 0, &rc);
	REQUIRE(rc == U_ILLEGAL_ARGUMENT_ERROR);

    pBiDi = getBiDiObject();
    ubidi_orderParagraphsLTR(pBiDi, true);

    /* test proLength < -1 */
    rc = U_ZERO_ERROR;
    ubidi_setContext(pBiDi, NULL, -2, NULL, 0, &rc);
	REQUIRE(rc == U_ILLEGAL_ARGUMENT_ERROR);
    /* test epiLength < -1 */
    rc = U_ZERO_ERROR;
    ubidi_setContext(pBiDi, NULL, 0, NULL, -2, &rc);
	REQUIRE(rc == U_ILLEGAL_ARGUMENT_ERROR);
    /* test prologue == NULL */
    rc = U_ZERO_ERROR;
    ubidi_setContext(pBiDi, NULL, 3, NULL, 0, &rc);
	REQUIRE(rc == U_ILLEGAL_ARGUMENT_ERROR);
    /* test epilogue == NULL */
    rc = U_ZERO_ERROR;
    ubidi_setContext(pBiDi, NULL, 0, NULL, 4, &rc);
	REQUIRE(rc == U_ILLEGAL_ARGUMENT_ERROR);

    for (tc = 0; tc < CONTEXT_COUNT; tc++) {
        cc = contextData[tc];
        proLength = (int32_t)strlen(cc.prologue);
        pseudoToU16(proLength, cc.prologue, prologue);
        epiLength = (int32_t)strlen(cc.epilogue);
        pseudoToU16(epiLength, cc.epilogue, epilogue);
        /* in the call below, prologue and epilogue are swapped to show
           that the next call will override this call */
        rc = U_ZERO_ERROR;
        ubidi_setContext(pBiDi, epilogue, epiLength, prologue, proLength, &rc);
		REQUIRE(U_SUCCESS(rc));
        ubidi_setContext(pBiDi, prologue, -1, epilogue, -1, &rc);
		REQUIRE(U_SUCCESS(rc));
        srcLen = (int32_t)strlen(cc.source);
        pseudoToU16(srcLen, cc.source, src);
        ubidi_setPara(pBiDi, src, srcLen, cc.paraLevel, NULL, &rc);
		REQUIRE(U_SUCCESS(rc));
        destLen = ubidi_writeReordered(pBiDi, dest, MAXLEN, UBIDI_DO_MIRRORING, &rc);
		REQUIRE(U_SUCCESS(rc));
        u16ToPseudo(destLen, dest, destChars);
        REQUIRE(uprv_strcmp(cc.expected, destChars) == 0);
    }
    ubidi_close(pBiDi);
}

TEST_CASE("TestBracketOverflow", "[BiDi]") {
	static const char* TEXT = "(((((((((((((((((((((((((((((((((((((((((a)(A)))))))))))))))))))))))))))))))))))))))))";
    UErrorCode status = U_ZERO_ERROR;
    UBiDi* bidi;
    UChar src[100];
    int32_t len;

    bidi = ubidi_open();
    len = (int32_t)uprv_strlen(TEXT);
    pseudoToU16(len, TEXT, src);
    ubidi_setPara(bidi, src, len, UBIDI_DEFAULT_LTR , NULL, &status);
	REQUIRE(U_SUCCESS(status));

    ubidi_close(bidi);
}

TEST_CASE("TestExplicitLevel0", "[BiDi]") {
	// The following used to fail with an error, see ICU ticket #12922.
    static const UChar text[2] = { 0x202d, 0x05d0 };
    static UBiDiLevel embeddings[2] = { 0, 0 };
    UErrorCode errorCode = U_ZERO_ERROR;
    UBiDi *bidi = ubidi_open();
    ubidi_setPara(bidi, text, 2, UBIDI_DEFAULT_LTR , embeddings, &errorCode);
	REQUIRE(U_SUCCESS(errorCode));
	UBiDiLevel level0 = ubidi_getLevelAt(bidi, 0);
	UBiDiLevel level1 = ubidi_getLevelAt(bidi, 1);
	REQUIRE(level0 == 1);
	REQUIRE(level1 == 1);
	REQUIRE(embeddings[0] == 1);
	REQUIRE(embeddings[1] == 1);
    ubidi_close(bidi);
}

TEST_CASE("ArabicShapingTest", "[BiDi]") {
	static const UChar
    source[]={
        0x31,   /* en:1 */
        0x627,  /* arabic:alef */
        0x32,   /* en:2 */
        0x6f3,  /* an:3 */
        0x61,   /* latin:a */
        0x34,   /* en:4 */
        0
    }, en2an[]={
        0x661, 0x627, 0x662, 0x6f3, 0x61, 0x664, 0
    }, an2en[]={
        0x31, 0x627, 0x32, 0x33, 0x61, 0x34, 0
    }, logical_alen2an_init_lr[]={
        0x31, 0x627, 0x662, 0x6f3, 0x61, 0x34, 0
    }, logical_alen2an_init_al[]={
        0x6f1, 0x627, 0x6f2, 0x6f3, 0x61, 0x34, 0
    }, reverse_alen2an_init_lr[]={
        0x661, 0x627, 0x32, 0x6f3, 0x61, 0x34, 0
    }, reverse_alen2an_init_al[]={
        0x6f1, 0x627, 0x32, 0x6f3, 0x61, 0x6f4, 0
    }, lamalef[]={
        0xfefb, 0
    };
    UChar dest[8];
    UErrorCode errorCode;
    int32_t length;

    /* test number shaping */

    /* european->arabic */
    errorCode=U_ZERO_ERROR;
    length=u_shapeArabic(source, UPRV_LENGTHOF(source),
                         dest, UPRV_LENGTHOF(dest),
                         U_SHAPE_DIGITS_EN2AN|U_SHAPE_DIGIT_TYPE_AN,
                         &errorCode);
	REQUIRE(U_SUCCESS(errorCode));
	REQUIRE(length == UPRV_LENGTHOF(source));
	REQUIRE(memcmp(dest, en2an, length*U_SIZEOF_UCHAR) == 0);

    /* arabic->european */
    errorCode=U_ZERO_ERROR;
    length=u_shapeArabic(source, -1,
                         dest, UPRV_LENGTHOF(dest),
                         U_SHAPE_DIGITS_AN2EN|U_SHAPE_DIGIT_TYPE_AN_EXTENDED,
                         &errorCode);
	REQUIRE(U_SUCCESS(errorCode));
	REQUIRE(length == u_strlen(source));
	REQUIRE(memcmp(dest, an2en, length*U_SIZEOF_UCHAR) == 0);

    /* european->arabic with context, logical order, initial state not AL */
    errorCode=U_ZERO_ERROR;
    length=u_shapeArabic(source, UPRV_LENGTHOF(source),
                         dest, UPRV_LENGTHOF(dest),
                         U_SHAPE_DIGITS_ALEN2AN_INIT_LR|U_SHAPE_DIGIT_TYPE_AN,
                         &errorCode);
	REQUIRE(U_SUCCESS(errorCode));
	REQUIRE(length == UPRV_LENGTHOF(source));
	REQUIRE(memcmp(dest, logical_alen2an_init_lr, length*U_SIZEOF_UCHAR) == 0);

    /* european->arabic with context, logical order, initial state AL */
    errorCode=U_ZERO_ERROR;
    length=u_shapeArabic(source, UPRV_LENGTHOF(source),
                         dest, UPRV_LENGTHOF(dest),
                         U_SHAPE_DIGITS_ALEN2AN_INIT_AL|U_SHAPE_DIGIT_TYPE_AN_EXTENDED,
                         &errorCode);
	REQUIRE(U_SUCCESS(errorCode));
	REQUIRE(length == UPRV_LENGTHOF(source));
	REQUIRE(memcmp(dest, logical_alen2an_init_al, length*U_SIZEOF_UCHAR) == 0);

    /* european->arabic with context, reverse order, initial state not AL */
    errorCode=U_ZERO_ERROR;
    length=u_shapeArabic(source, UPRV_LENGTHOF(source),
                         dest, UPRV_LENGTHOF(dest),
                         U_SHAPE_DIGITS_ALEN2AN_INIT_LR|U_SHAPE_DIGIT_TYPE_AN|U_SHAPE_TEXT_DIRECTION_VISUAL_LTR,
                         &errorCode);
	REQUIRE(U_SUCCESS(errorCode));
	REQUIRE(length == UPRV_LENGTHOF(source));
	REQUIRE(memcmp(dest, reverse_alen2an_init_lr, length*U_SIZEOF_UCHAR) == 0);

    /* european->arabic with context, reverse order, initial state AL */
    errorCode=U_ZERO_ERROR;
    length=u_shapeArabic(source, UPRV_LENGTHOF(source),
                         dest, UPRV_LENGTHOF(dest),
                         U_SHAPE_DIGITS_ALEN2AN_INIT_AL|U_SHAPE_DIGIT_TYPE_AN_EXTENDED|U_SHAPE_TEXT_DIRECTION_VISUAL_LTR,
                         &errorCode);
	REQUIRE(U_SUCCESS(errorCode));
	REQUIRE(length == UPRV_LENGTHOF(source));
	REQUIRE(memcmp(dest, reverse_alen2an_init_al, length*U_SIZEOF_UCHAR) == 0);

    /* test noop */
    errorCode=U_ZERO_ERROR;
    length=u_shapeArabic(source, UPRV_LENGTHOF(source),
                         dest, UPRV_LENGTHOF(dest),
                         0,
                         &errorCode);
	REQUIRE(U_SUCCESS(errorCode));
	REQUIRE(length == UPRV_LENGTHOF(source));
	REQUIRE(memcmp(dest, source, length*U_SIZEOF_UCHAR) == 0);

    errorCode=U_ZERO_ERROR;
    length=u_shapeArabic(source, 0,
                         dest, UPRV_LENGTHOF(dest),
                         U_SHAPE_DIGITS_EN2AN|U_SHAPE_DIGIT_TYPE_AN,
                         &errorCode);
	REQUIRE(U_SUCCESS(errorCode));
	REQUIRE(length == 0);

    /* preflight digit shaping */
    errorCode=U_ZERO_ERROR;
    length=u_shapeArabic(source, UPRV_LENGTHOF(source),
                         NULL, 0,
                         U_SHAPE_DIGITS_EN2AN|U_SHAPE_DIGIT_TYPE_AN,
                         &errorCode);
	REQUIRE(errorCode == U_BUFFER_OVERFLOW_ERROR);
	REQUIRE(length == UPRV_LENGTHOF(source));

    /* test illegal arguments */
    errorCode=U_ZERO_ERROR;
    length=u_shapeArabic(NULL, UPRV_LENGTHOF(source),
                         dest, UPRV_LENGTHOF(dest),
                         U_SHAPE_DIGITS_EN2AN|U_SHAPE_DIGIT_TYPE_AN,
                         &errorCode);
	REQUIRE(errorCode == U_ILLEGAL_ARGUMENT_ERROR);

    errorCode=U_ZERO_ERROR;
    length=u_shapeArabic(source, -2,
                         dest, UPRV_LENGTHOF(dest),
                         U_SHAPE_DIGITS_EN2AN|U_SHAPE_DIGIT_TYPE_AN,
                         &errorCode);
	REQUIRE(errorCode == U_ILLEGAL_ARGUMENT_ERROR);

    errorCode=U_ZERO_ERROR;
    length=u_shapeArabic(source, UPRV_LENGTHOF(source),
                         NULL, UPRV_LENGTHOF(dest),
                         U_SHAPE_DIGITS_EN2AN|U_SHAPE_DIGIT_TYPE_AN,
                         &errorCode);
	REQUIRE(errorCode == U_ILLEGAL_ARGUMENT_ERROR);

    errorCode=U_ZERO_ERROR;
    length=u_shapeArabic(source, UPRV_LENGTHOF(source),
                         dest, -1,
                         U_SHAPE_DIGITS_EN2AN|U_SHAPE_DIGIT_TYPE_AN,
                         &errorCode);
	REQUIRE(errorCode == U_ILLEGAL_ARGUMENT_ERROR);

    errorCode=U_ZERO_ERROR;
    length=u_shapeArabic(source, UPRV_LENGTHOF(source),
                         dest, UPRV_LENGTHOF(dest),
                         U_SHAPE_DIGITS_RESERVED|U_SHAPE_DIGIT_TYPE_AN,
                         &errorCode);
	REQUIRE(errorCode == U_ILLEGAL_ARGUMENT_ERROR);

    errorCode=U_ZERO_ERROR;
    length=u_shapeArabic(source, UPRV_LENGTHOF(source),
                         dest, UPRV_LENGTHOF(dest),
                         U_SHAPE_DIGITS_EN2AN|U_SHAPE_DIGIT_TYPE_RESERVED,
                         &errorCode);
	REQUIRE(errorCode == U_ILLEGAL_ARGUMENT_ERROR);

    errorCode=U_ZERO_ERROR;
    length=u_shapeArabic(source, UPRV_LENGTHOF(source),
                         (UChar *)(source+2), UPRV_LENGTHOF(dest), /* overlap source and destination */
                         U_SHAPE_DIGITS_EN2AN|U_SHAPE_DIGIT_TYPE_AN,
                         &errorCode);
	REQUIRE(errorCode == U_ILLEGAL_ARGUMENT_ERROR);

    errorCode=U_ZERO_ERROR;
    length=u_shapeArabic(lamalef, UPRV_LENGTHOF(lamalef),
                         dest, UPRV_LENGTHOF(dest),
                         U_SHAPE_LETTERS_UNSHAPE | U_SHAPE_LENGTH_GROW_SHRINK | U_SHAPE_TEXT_DIRECTION_VISUAL_LTR,
                         &errorCode);
	REQUIRE(U_SUCCESS(errorCode));
	REQUIRE(length != UPRV_LENGTHOF(lamalef));
}

static void doTests(UBiDi *pBiDi, UBiDi *pLine, UBool countRunsFirst) {
    UChar string[MAXLEN];
    int32_t lineStart;
    UBiDiLevel paraLevel;

    for (int testNumber = 0; testNumber < bidiTestCount; ++testNumber) {
        UErrorCode errorCode = U_ZERO_ERROR;
        getStringFromDirProps(tests[testNumber].text, tests[testNumber].length, string);
        paraLevel=tests[testNumber].paraLevel;
        ubidi_setPara(pBiDi, string, -1, paraLevel, NULL, &errorCode);
		REQUIRE(U_SUCCESS(errorCode));

		lineStart=tests[testNumber].lineStart;

		if (lineStart==-1) {
			doTest(pBiDi, testNumber, tests+testNumber, 0, countRunsFirst);
		}
		else {
			ubidi_setLine(pBiDi, lineStart, tests[testNumber].lineLimit, pLine, &errorCode);
			REQUIRE(U_SUCCESS(errorCode));
			doTest(pLine, testNumber, tests+testNumber, lineStart, countRunsFirst);
		}
    }
}

static void doTest(UBiDi *pBiDi, int testNumber, const BiDiTestData *test, int32_t lineStart,
		UBool countRunsFirst) {
    const uint8_t *dirProps= (test->text == NULL) ? NULL : test->text+lineStart;
    const UBiDiLevel *levels=test->levels;
    const uint8_t *visualMap=test->visualMap;
    int32_t i, len=ubidi_getLength(pBiDi), logicalIndex, runCount = 0;
    UErrorCode errorCode=U_ZERO_ERROR;
    UBiDiLevel level, level2;

    if (countRunsFirst) {
        runCount = ubidi_countRuns(pBiDi, &errorCode);
		REQUIRE(U_SUCCESS(errorCode));
    }

    _testReordering(pBiDi, testNumber);

    REQUIRE(test->direction == ubidi_getDirection(pBiDi));
    REQUIRE(test->resultLevel == ubidi_getParaLevel(pBiDi));

    for(i=0; i<len; ++i) {
        REQUIRE(levels[i] == ubidi_getLevelAt(pBiDi, i));
    }

    for(i=0; i<len; ++i) {
        logicalIndex=ubidi_getVisualIndex(pBiDi, i, &errorCode);
		REQUIRE(U_SUCCESS(errorCode));
		REQUIRE(visualMap[i] == logicalIndex);
    }

    if (!countRunsFirst) {
        runCount=ubidi_countRuns(pBiDi, &errorCode);
		REQUIRE(U_SUCCESS(errorCode));
    }

    for(logicalIndex=0; logicalIndex<len;) {
        level=ubidi_getLevelAt(pBiDi, logicalIndex);
        ubidi_getLogicalRun(pBiDi, logicalIndex, &logicalIndex, &level2);
		CHECK(level == level2);
		REQUIRE(--runCount >= 0);
    }

	REQUIRE(runCount == 0);
}

static void doMisc() {
	/* Miscellaneous tests to exercize less popular code paths */
    UBiDi *bidi, *bidiLine;
    UChar src[MAXLEN], dest[MAXLEN];
    int32_t srcLen, destLen, runCount, i;
    UBiDiLevel level;
    UBiDiDirection dir;
    int32_t map[MAXLEN];
    UErrorCode errorCode=U_ZERO_ERROR;
    static const int32_t srcMap[6] = {0,1,-1,5,4};
    static const int32_t dstMap[6] = {0,1,-1,-1,4,3};

    bidi = ubidi_openSized(120, 66, &errorCode);
	REQUIRE(bidi != nullptr);

    bidiLine = ubidi_open();
	REQUIRE(bidiLine != nullptr);

    destLen = ubidi_writeReverse(src, 0, dest, MAXLEN, 0, &errorCode);
	CHECK(destLen == 0);
	REQUIRE(U_SUCCESS(errorCode));

    ubidi_setPara(bidi, src, 0, UBIDI_LTR, NULL, &errorCode);
    destLen = ubidi_writeReordered(bidi, dest, MAXLEN, 0, &errorCode);
	REQUIRE(destLen == 0);
    REQUIRE(U_SUCCESS(errorCode));

    srcLen = u_unescape("abc       ", src, MAXLEN);
    ubidi_setPara(bidi, src, srcLen, UBIDI_RTL, NULL, &errorCode);
    ubidi_setLine(bidi, 0, 6, bidiLine, &errorCode);
    for (i = 3; i < 6; i++) {
        level = ubidi_getLevelAt(bidiLine, i);
		CHECK(level == UBIDI_RTL);
    }
    REQUIRE(U_SUCCESS(errorCode));

    srcLen = u_unescape("abc       def", src, MAXLEN);
    ubidi_setPara(bidi, src, srcLen, UBIDI_RTL, NULL, &errorCode);
    ubidi_setLine(bidi, 0, 6, bidiLine, &errorCode);
    for (i = 3; i < 6; i++) {
        level = ubidi_getLevelAt(bidiLine, i);
		CHECK(level == UBIDI_RTL);
    }
    REQUIRE(U_SUCCESS(errorCode));

    srcLen = u_unescape("abcdefghi    ", src, MAXLEN);
    ubidi_setPara(bidi, src, srcLen, UBIDI_RTL, NULL, &errorCode);
    ubidi_setLine(bidi, 0, 6, bidiLine, &errorCode);
    for (i = 3; i < 6; i++) {
        level = ubidi_getLevelAt(bidiLine, i);
		CHECK(level == 2);
    }
    REQUIRE(U_SUCCESS(errorCode));

    ubidi_setReorderingOptions(bidi, UBIDI_OPTION_REMOVE_CONTROLS);
    srcLen = u_unescape("\\u200eabc       def", src, MAXLEN);
    ubidi_setPara(bidi, src, srcLen, UBIDI_RTL, NULL, &errorCode);
    ubidi_setLine(bidi, 0, 6, bidiLine, &errorCode);
    destLen = ubidi_getResultLength(bidiLine);
	REQUIRE(destLen == 5);
    REQUIRE(U_SUCCESS(errorCode));

    srcLen = u_unescape("abcdefghi", src, MAXLEN);
    ubidi_setPara(bidi, src, srcLen, UBIDI_LTR, NULL, &errorCode);
    ubidi_setLine(bidi, 0, 6, bidiLine, &errorCode);
    dir = ubidi_getDirection(bidiLine);
	REQUIRE(dir == UBIDI_LTR);
    REQUIRE(U_SUCCESS(errorCode));

    ubidi_setPara(bidi, src, 0, UBIDI_LTR, NULL, &errorCode);
    runCount = ubidi_countRuns(bidi, &errorCode);
	REQUIRE(runCount == 0);
    REQUIRE(U_SUCCESS(errorCode));

    srcLen = u_unescape("          ", src, MAXLEN);
    ubidi_setPara(bidi, src, srcLen, UBIDI_RTL, NULL, &errorCode);
    ubidi_setLine(bidi, 0, 6, bidiLine, &errorCode);
    runCount = ubidi_countRuns(bidiLine, &errorCode);
	REQUIRE(runCount == 1);
    REQUIRE(U_SUCCESS(errorCode));

    srcLen = u_unescape("a\\u05d0        bc", src, MAXLEN);
    ubidi_setPara(bidi, src, srcLen, UBIDI_RTL, NULL, &errorCode);
    ubidi_setLine(bidi, 0, 6, bidiLine, &errorCode);
    dir = ubidi_getDirection(bidi);
	REQUIRE(dir == UBIDI_MIXED);
    dir = ubidi_getDirection(bidiLine);
	REQUIRE(dir == UBIDI_MIXED);
    runCount = ubidi_countRuns(bidiLine, &errorCode);
	REQUIRE(runCount == 2);
    REQUIRE(U_SUCCESS(errorCode));

    ubidi_invertMap(srcMap, map, 5);
	REQUIRE(memcmp(dstMap, map, sizeof(dstMap)) == 0);

    /* test REMOVE_BIDI_CONTROLS together with DO_MIRRORING */
    srcLen = u_unescape("abc\\u200e", src, MAXLEN);
    ubidi_setPara(bidi, src, srcLen, UBIDI_LTR, NULL, &errorCode);
    destLen = ubidi_writeReordered(bidi, dest, MAXLEN,
              UBIDI_REMOVE_BIDI_CONTROLS | UBIDI_DO_MIRRORING, &errorCode);
	REQUIRE(destLen == 3);
	REQUIRE(memcmp(dest, src, 3 * sizeof(UChar)) == 0);
    REQUIRE(U_SUCCESS(errorCode));

    /* test inverse Bidi with marks and contextual orientation */
    ubidi_setReorderingMode(bidi, UBIDI_REORDER_INVERSE_LIKE_DIRECT);
    ubidi_setReorderingOptions(bidi, UBIDI_OPTION_INSERT_MARKS);
    ubidi_setPara(bidi, src, 0, UBIDI_DEFAULT_RTL, NULL, &errorCode);
    destLen = ubidi_writeReordered(bidi, dest, MAXLEN, 0, &errorCode);
	REQUIRE(destLen == 0);
    REQUIRE(U_SUCCESS(errorCode));
    srcLen = u_unescape("   ", src, MAXLEN);
    ubidi_setPara(bidi, src, srcLen, UBIDI_DEFAULT_RTL, NULL, &errorCode);
    destLen = ubidi_writeReordered(bidi, dest, MAXLEN, 0, &errorCode);
	REQUIRE(destLen == 3);
	REQUIRE(memcmp(dest, src, destLen * sizeof(UChar)) == 0);
    REQUIRE(U_SUCCESS(errorCode));
    srcLen = u_unescape("abc", src, MAXLEN);
    ubidi_setPara(bidi, src, srcLen, UBIDI_DEFAULT_RTL, NULL, &errorCode);
    destLen = ubidi_writeReordered(bidi, dest, MAXLEN, 0, &errorCode);
	REQUIRE(destLen == 3);
	REQUIRE(memcmp(dest, src, destLen * sizeof(UChar)) == 0);
    REQUIRE(U_SUCCESS(errorCode));
    srcLen = u_unescape("\\u05d0\\u05d1", src, MAXLEN);
    ubidi_setPara(bidi, src, srcLen, UBIDI_DEFAULT_RTL, NULL, &errorCode);
    destLen = ubidi_writeReordered(bidi, dest, MAXLEN, 0, &errorCode);
    srcLen = u_unescape("\\u05d1\\u05d0", src, MAXLEN);
	REQUIRE(destLen == 2);
	REQUIRE(memcmp(dest, src, destLen * sizeof(UChar)) == 0);
    REQUIRE(U_SUCCESS(errorCode));
    srcLen = u_unescape("abc \\u05d0\\u05d1", src, MAXLEN);
    ubidi_setPara(bidi, src, srcLen, UBIDI_DEFAULT_RTL, NULL, &errorCode);
    destLen = ubidi_writeReordered(bidi, dest, MAXLEN, 0, &errorCode);
    srcLen = u_unescape("\\u05d1\\u05d0 abc", src, MAXLEN);
	REQUIRE(destLen == 6);
	REQUIRE(memcmp(dest, src, destLen * sizeof(UChar)) == 0);
    REQUIRE(U_SUCCESS(errorCode));
    srcLen = u_unescape("\\u05d0\\u05d1 abc", src, MAXLEN);
    ubidi_setPara(bidi, src, srcLen, UBIDI_DEFAULT_RTL, NULL, &errorCode);
    destLen = ubidi_writeReordered(bidi, dest, MAXLEN, 0, &errorCode);
    srcLen = u_unescape("\\u200fabc \\u05d1\\u05d0", src, MAXLEN);
	REQUIRE(destLen == 7);
	REQUIRE(memcmp(dest, src, destLen * sizeof(UChar)) == 0);
    REQUIRE(U_SUCCESS(errorCode));
    srcLen = u_unescape("\\u05d0\\u05d1 abc .-=", src, MAXLEN);
    ubidi_setPara(bidi, src, srcLen, UBIDI_DEFAULT_RTL, NULL, &errorCode);
    destLen = ubidi_writeReordered(bidi, dest, MAXLEN, 0, &errorCode);
    srcLen = u_unescape("\\u200f=-. abc \\u05d1\\u05d0", src, MAXLEN);
	REQUIRE(destLen == 11);
	REQUIRE(memcmp(dest, src, destLen * sizeof(UChar)) == 0);
    REQUIRE(U_SUCCESS(errorCode));
    ubidi_orderParagraphsLTR(bidi, true);
    srcLen = u_unescape("\n\r   \n\rabc\n\\u05d0\\u05d1\rabc \\u05d2\\u05d3\n\r"
                        "\\u05d4\\u05d5 abc\n\\u05d6\\u05d7 abc .-=\r\n"
                        "-* \\u05d8\\u05d9 abc .-=", src, MAXLEN);
    ubidi_setPara(bidi, src, srcLen, UBIDI_DEFAULT_RTL, NULL, &errorCode);
    destLen = ubidi_writeReordered(bidi, dest, MAXLEN, 0, &errorCode);
    srcLen = u_unescape("\n\r   \n\rabc\n\\u05d1\\u05d0\r\\u05d3\\u05d2 abc\n\r"
                        "\\u200fabc \\u05d5\\u05d4\n\\u200f=-. abc \\u05d7\\u05d6\r\n"
                        "\\u200f=-. abc \\u05d9\\u05d8 *-", src, MAXLEN);
	REQUIRE(destLen == 57);
	REQUIRE(memcmp(dest, src, destLen * sizeof(UChar)) == 0);
    REQUIRE(U_SUCCESS(errorCode));
    srcLen = u_unescape("\\u05d0 \t", src, MAXLEN);
    ubidi_setPara(bidi, src, srcLen, UBIDI_LTR, NULL, &errorCode);
    destLen = ubidi_writeReordered(bidi, dest, MAXLEN, 0, &errorCode);
    srcLen = u_unescape("\\u05D0\\u200e \t", src, MAXLEN);
	REQUIRE(destLen == 4);
	REQUIRE(memcmp(dest, src, destLen * sizeof(UChar)) == 0);
    REQUIRE(U_SUCCESS(errorCode));
    srcLen = u_unescape("\\u05d0 123 \t\\u05d1 123 \\u05d2", src, MAXLEN);
    ubidi_setPara(bidi, src, srcLen, UBIDI_LTR, NULL, &errorCode);
    destLen = ubidi_writeReordered(bidi, dest, MAXLEN, 0, &errorCode);
    srcLen = u_unescape("\\u05d0 \\u200e123\\u200e \t\\u05d2 123 \\u05d1", src, MAXLEN);
	REQUIRE(destLen == 16);
	REQUIRE(memcmp(dest, src, destLen * sizeof(UChar)) == 0);
    REQUIRE(U_SUCCESS(errorCode));
    srcLen = u_unescape("\\u05d0 123 \\u0660\\u0661 ab", src, MAXLEN);
    ubidi_setPara(bidi, src, srcLen, UBIDI_LTR, NULL, &errorCode);
    destLen = ubidi_writeReordered(bidi, dest, MAXLEN, 0, &errorCode);
    srcLen = u_unescape("\\u05d0 \\u200e123 \\u200e\\u0660\\u0661 ab", src, MAXLEN);
	REQUIRE(destLen == 13);
	REQUIRE(memcmp(dest, src, destLen * sizeof(UChar)) == 0);
    REQUIRE(U_SUCCESS(errorCode));
    srcLen = u_unescape("ab \t", src, MAXLEN);
    ubidi_setPara(bidi, src, srcLen, UBIDI_RTL, NULL, &errorCode);
    destLen = ubidi_writeReordered(bidi, dest, MAXLEN, 0, &errorCode);
    srcLen = u_unescape("\\u200f\t ab", src, MAXLEN);
	REQUIRE(destLen == 5);
	REQUIRE(memcmp(dest, src, destLen * sizeof(UChar)) == 0);
    REQUIRE(U_SUCCESS(errorCode));

    /* check exceeding para level */
    ubidi_close(bidi);
    bidi = ubidi_open();
    srcLen = u_unescape("A\\u202a\\u05d0\\u202aC\\u202c\\u05d1\\u202cE", src, MAXLEN);
    ubidi_setPara(bidi, src, srcLen, UBIDI_MAX_EXPLICIT_LEVEL - 1, NULL, &errorCode);
    level = ubidi_getLevelAt(bidi, 2);
	REQUIRE(level == UBIDI_MAX_EXPLICIT_LEVEL);
    REQUIRE(U_SUCCESS(errorCode));

    /* check 1-char runs with RUNS_ONLY */
    ubidi_setReorderingMode(bidi, UBIDI_REORDER_RUNS_ONLY);
    srcLen = u_unescape("a \\u05d0 b \\u05d1 c \\u05d2 d ", src, MAXLEN);
    ubidi_setPara(bidi, src, srcLen, UBIDI_LTR, NULL, &errorCode);
    runCount = ubidi_countRuns(bidi, &errorCode);
	REQUIRE(runCount == 14);
    REQUIRE(U_SUCCESS(errorCode));

    ubidi_close(bidi);
    ubidi_close(bidiLine);
}

static void _testReordering(UBiDi *pBiDi, int testNumber) {
    int32_t
        logicalMap1[MAXLEN], logicalMap2[MAXLEN], logicalMap3[MAXLEN],
        visualMap1[MAXLEN], visualMap2[MAXLEN], visualMap3[MAXLEN], visualMap4[MAXLEN];
    UErrorCode errorCode=U_ZERO_ERROR;
    const UBiDiLevel *levels;
    int32_t i, length=ubidi_getLength(pBiDi),
               destLength=ubidi_getResultLength(pBiDi);
    int32_t runCount, visualIndex, logicalStart, runLength;
    UBool odd;

    if (length<=0) {
        return;
    }

    /* get the logical and visual maps from the object */
    ubidi_getLogicalMap(pBiDi, logicalMap1, &errorCode);
	REQUIRE(U_SUCCESS(errorCode));

    ubidi_getVisualMap(pBiDi, visualMap1, &errorCode);
	REQUIRE(U_SUCCESS(errorCode));

    /* invert them both */
    ubidi_invertMap(logicalMap1, visualMap2, length);
    ubidi_invertMap(visualMap1, logicalMap2, destLength);

    /* get them from the levels array, too */
    levels=ubidi_getLevels(pBiDi, &errorCode);
	REQUIRE(U_SUCCESS(errorCode));

    ubidi_reorderLogical(levels, length, logicalMap3);
    ubidi_reorderVisual(levels, length, visualMap3);

    /* get the visual map from the runs, too */
    runCount=ubidi_countRuns(pBiDi, &errorCode);
	REQUIRE(U_SUCCESS(errorCode));

    visualIndex=0;
    for(i=0; i<runCount; ++i) {
        odd=(UBool)ubidi_getVisualRun(pBiDi, i, &logicalStart, &runLength);
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

    /* check that the indexes are the same between these and ubidi_getLogical/VisualIndex() */
    for(i=0; i<length; ++i) {
        REQUIRE(logicalMap1[i] == logicalMap2[i]);
        REQUIRE(logicalMap1[i] == logicalMap3[i]);
        REQUIRE(visualMap1[i] == visualMap2[i]);
        REQUIRE(visualMap1[i] == visualMap3[i]);
        REQUIRE(visualMap1[i] == visualMap4[i]);
        REQUIRE(logicalMap1[i] == ubidi_getVisualIndex(pBiDi, i, &errorCode));
        REQUIRE(U_SUCCESS(errorCode));
        REQUIRE(visualMap1[i] == ubidi_getLogicalIndex(pBiDi, i, &errorCode));
        REQUIRE(U_SUCCESS(errorCode));
    }
}

static const char columns[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

#define TABLE_SIZE  256
static UBool   tablesInitialized = false;
static UChar   pseudoToUChar[TABLE_SIZE];
static uint8_t UCharToPseudo[TABLE_SIZE];    /* used for Unicode chars < 0x0100 */
static uint8_t UCharToPseud2[TABLE_SIZE];    /* used for Unicode chars >=0x0100 */

static void buildPseudoTables(void)
/*
    The rules for pseudo-Bidi are as follows:
    - [ == LRE
    - ] == RLE
    - { == LRO
    - } == RLO
    - ^ == PDF
    - @ == LRM
    - & == RLM
    - A-F == Arabic Letters 0631-0636
    - G-V == Hebrew letters 05d7-05e6
    - W-Z == Unassigned RTL 05CC..05CF
        originally 08D0..08D3
        Unicode 6.1 changes U+08A0..U+08FF from R to AL which works ok.
        Unicode 11 adds U+08D3 ARABIC SMALL LOW WAW which has bc=NSM
            so we stop using Z in this test.
        Unicode 14 assigns 08D0..08D2 to diacritics (bc=NSM) so we switch to 05CC..05CF.
    - 0-5 == western digits 0030-0035
    - 6-9 == Arabic-Indic digits 0666-0669
    - ` == Combining Grave Accent 0300 (NSM)
    - ~ == Delete 007f (BN)
    - | == Paragraph Separator 2029 (B)
    - _ == Info Separator 1 001f (S)
    All other characters represent themselves as Latin-1, with the corresponding
    Bidi properties.
*/
{
    int             i;
    UChar           uchar;
    uint8_t         c;
    /* initialize all tables to unknown */
    for (i=0; i < TABLE_SIZE; i++) {
        pseudoToUChar[i] = 0xFFFD;
        UCharToPseudo[i] = '?';
        UCharToPseud2[i] = '?';
    }
    /* initialize non letters or digits */
    pseudoToUChar[(uint8_t) 0 ] = 0x0000;    UCharToPseudo[0x00] = (uint8_t) 0 ;
    pseudoToUChar[(uint8_t)' '] = 0x0020;    UCharToPseudo[0x20] = (uint8_t)' ';
    pseudoToUChar[(uint8_t)'!'] = 0x0021;    UCharToPseudo[0x21] = (uint8_t)'!';
    pseudoToUChar[(uint8_t)'"'] = 0x0022;    UCharToPseudo[0x22] = (uint8_t)'"';
    pseudoToUChar[(uint8_t)'#'] = 0x0023;    UCharToPseudo[0x23] = (uint8_t)'#';
    pseudoToUChar[(uint8_t)'$'] = 0x0024;    UCharToPseudo[0x24] = (uint8_t)'$';
    pseudoToUChar[(uint8_t)'%'] = 0x0025;    UCharToPseudo[0x25] = (uint8_t)'%';
    pseudoToUChar[(uint8_t)'\'']= 0x0027;    UCharToPseudo[0x27] = (uint8_t)'\'';
    pseudoToUChar[(uint8_t)'('] = 0x0028;    UCharToPseudo[0x28] = (uint8_t)'(';
    pseudoToUChar[(uint8_t)')'] = 0x0029;    UCharToPseudo[0x29] = (uint8_t)')';
    pseudoToUChar[(uint8_t)'*'] = 0x002A;    UCharToPseudo[0x2A] = (uint8_t)'*';
    pseudoToUChar[(uint8_t)'+'] = 0x002B;    UCharToPseudo[0x2B] = (uint8_t)'+';
    pseudoToUChar[(uint8_t)','] = 0x002C;    UCharToPseudo[0x2C] = (uint8_t)',';
    pseudoToUChar[(uint8_t)'-'] = 0x002D;    UCharToPseudo[0x2D] = (uint8_t)'-';
    pseudoToUChar[(uint8_t)'.'] = 0x002E;    UCharToPseudo[0x2E] = (uint8_t)'.';
    pseudoToUChar[(uint8_t)'/'] = 0x002F;    UCharToPseudo[0x2F] = (uint8_t)'/';
    pseudoToUChar[(uint8_t)':'] = 0x003A;    UCharToPseudo[0x3A] = (uint8_t)':';
    pseudoToUChar[(uint8_t)';'] = 0x003B;    UCharToPseudo[0x3B] = (uint8_t)';';
    pseudoToUChar[(uint8_t)'<'] = 0x003C;    UCharToPseudo[0x3C] = (uint8_t)'<';
    pseudoToUChar[(uint8_t)'='] = 0x003D;    UCharToPseudo[0x3D] = (uint8_t)'=';
    pseudoToUChar[(uint8_t)'>'] = 0x003E;    UCharToPseudo[0x3E] = (uint8_t)'>';
    pseudoToUChar[(uint8_t)'?'] = 0x003F;    UCharToPseudo[0x3F] = (uint8_t)'?';
    pseudoToUChar[(uint8_t)'\\']= 0x005C;    UCharToPseudo[0x5C] = (uint8_t)'\\';
    /* initialize specially used characters */
    pseudoToUChar[(uint8_t)'`'] = 0x0300;    UCharToPseud2[0x00] = (uint8_t)'`';  /* NSM */
    pseudoToUChar[(uint8_t)'@'] = 0x200E;    UCharToPseud2[0x0E] = (uint8_t)'@';  /* LRM */
    pseudoToUChar[(uint8_t)'&'] = 0x200F;    UCharToPseud2[0x0F] = (uint8_t)'&';  /* RLM */
    pseudoToUChar[(uint8_t)'_'] = 0x001F;    UCharToPseudo[0x1F] = (uint8_t)'_';  /* S   */
    pseudoToUChar[(uint8_t)'|'] = 0x2029;    UCharToPseud2[0x29] = (uint8_t)'|';  /* B   */
    pseudoToUChar[(uint8_t)'['] = 0x202A;    UCharToPseud2[0x2A] = (uint8_t)'[';  /* LRE */
    pseudoToUChar[(uint8_t)']'] = 0x202B;    UCharToPseud2[0x2B] = (uint8_t)']';  /* RLE */
    pseudoToUChar[(uint8_t)'^'] = 0x202C;    UCharToPseud2[0x2C] = (uint8_t)'^';  /* PDF */
    pseudoToUChar[(uint8_t)'{'] = 0x202D;    UCharToPseud2[0x2D] = (uint8_t)'{';  /* LRO */
    pseudoToUChar[(uint8_t)'}'] = 0x202E;    UCharToPseud2[0x2E] = (uint8_t)'}';  /* RLO */
    pseudoToUChar[(uint8_t)'~'] = 0x007F;    UCharToPseudo[0x7F] = (uint8_t)'~';  /* BN  */
    /* initialize western digits */
    for (i = 0, uchar = 0x0030; i < 6; i++, uchar++) {
        c = (uint8_t)columns[i];
        pseudoToUChar[c] = uchar;
        UCharToPseudo[uchar & 0x00ff] = c;
    }
    /* initialize Hindi digits */
    for (i = 6, uchar = 0x0666; i < 10; i++, uchar++) {
        c = (uint8_t)columns[i];
        pseudoToUChar[c] = uchar;
        UCharToPseud2[uchar & 0x00ff] = c;
    }
    /* initialize Arabic letters */
    for (i = 10, uchar = 0x0631; i < 16; i++, uchar++) {
        c = (uint8_t)columns[i];
        pseudoToUChar[c] = uchar;
        UCharToPseud2[uchar & 0x00ff] = c;
    }
    /* initialize Hebrew letters */
    for (i = 16, uchar = 0x05D7; i < 32; i++, uchar++) {
        c = (uint8_t)columns[i];
        pseudoToUChar[c] = uchar;
        UCharToPseud2[uchar & 0x00ff] = c;
    }
    /* initialize Unassigned code points */
    for (i = 32, uchar=0x05CC; i < 36; i++, uchar++) {
        c = (uint8_t)columns[i];
        pseudoToUChar[c] = uchar;
        UCharToPseud2[uchar & 0x00ff] = c;
    }
    /* initialize Latin lower case letters */
    for (i = 36, uchar = 0x0061; i < 62; i++, uchar++) {
        c = (uint8_t)columns[i];
        pseudoToUChar[c] = uchar;
        UCharToPseudo[uchar & 0x00ff] = c;
    }
    tablesInitialized = true;
}

/*----------------------------------------------------------------------*/

static int pseudoToU16(const int length, const char * input, UChar * output)
/*  This function converts a pseudo-Bidi string into a UChar string.
    It returns the length of the UChar string.
*/
{
    int             i;
    if (!tablesInitialized) {
        buildPseudoTables();
    }
    for (i = 0; i < length; i++)
        output[i] = pseudoToUChar[(uint8_t)input[i]];
    output[length] = 0;
    return length;
}

/*----------------------------------------------------------------------*/

static int u16ToPseudo(const int length, const UChar * input, char * output)
/*  This function converts a UChar string into a pseudo-Bidi string.
    It returns the length of the pseudo-Bidi string.
*/
{
    int             i;
    UChar           uchar;
    if (!tablesInitialized) {
        buildPseudoTables();
    }
    for (i = 0; i < length; i++)
    {
        uchar = input[i];
        output[i] = uchar < 0x0100 ? UCharToPseudo[uchar] :
                                        UCharToPseud2[uchar & 0x00ff];
    }
    output[length] = '\0';
    return length;
}

static char * formatLevels(UBiDi *bidi, char *buffer) {
    UErrorCode ec = U_ZERO_ERROR;
    const UBiDiLevel* gotLevels = ubidi_getLevels(bidi, &ec);
    int32_t len = ubidi_getLength(bidi);
    char c;
    int32_t i, k;

    if(U_FAILURE(ec)) {
        strcpy(buffer, "BAD LEVELS");
        return buffer;
    }
    for (i=0; i<len; i++) {
        k = gotLevels[i];
        if (k >= (int32_t)sizeof(columns))
            c = '+';
        else
            c = columns[k];
        buffer[i] = c;
    }
    buffer[len] = '\0';
    return buffer;
}
static const char *reorderingModeNames[] = {
    "UBIDI_REORDER_DEFAULT",
    "UBIDI_REORDER_NUMBERS_SPECIAL",
    "UBIDI_REORDER_GROUP_NUMBERS_WITH_R",
    "UBIDI_REORDER_RUNS_ONLY",
    "UBIDI_REORDER_INVERSE_NUMBERS_AS_L",
    "UBIDI_REORDER_INVERSE_LIKE_DIRECT",
    "UBIDI_REORDER_INVERSE_FOR_NUMBERS_SPECIAL"};

static char *reorderingOptionNames(char *buffer, int options) {
    buffer[0] = 0;
    if (options & UBIDI_OPTION_INSERT_MARKS) {
        strcat(buffer, " UBIDI_OPTION_INSERT_MARKS");
    }
    if (options & UBIDI_OPTION_REMOVE_CONTROLS) {
        strcat(buffer, " UBIDI_OPTION_REMOVE_CONTROLS");
    }
    if (options & UBIDI_OPTION_STREAMING) {
        strcat(buffer, " UBIDI_OPTION_STREAMING");
    }
    return buffer;
}

static UBool matchingPair(UBiDi *bidi, int32_t i, char c1, char c2)
{
    /* No test for []{} since they have special meaning for pseudo Bidi */
    static char mates1Chars[] = "<>()";
    static char mates2Chars[] = "><)(";
    UBiDiLevel level;
    int k, len;

    if (c1 == c2) {
        return true;
    }
    /* For UBIDI_REORDER_RUNS_ONLY, it would not be correct to check levels[i],
       so we use the appropriate run's level, which is good for all cases.
     */
    ubidi_getLogicalRun(bidi, i, NULL, &level);
    if ((level & 1) == 0) {
        return false;
    }
    len = (int)strlen(mates1Chars);
    for (k = 0; k < len; k++) {
        if ((c1 == mates1Chars[k]) && (c2 == mates2Chars[k])) {
            return true;
        }
    }
    return false;
}

static void printCaseInfo(UBiDi *bidi, const char *src, const char *dst)
/* src and dst are char arrays encoded as pseudo Bidi */
{
    /* Since calls to printf with a \n within the pattern increment the
     * error count, new lines are issued via fputs, except when we want the
     * increment to happen.
     */
    UErrorCode errorCode=U_ZERO_ERROR;
    int32_t i, length = ubidi_getProcessedLength(bidi);
    const UBiDiLevel *levels;
    char levelChars[MAXLEN];
    UBiDiLevel lev;
    int32_t runCount;
    char buffer[100];
    printf("========================================"); fputs("\n", stderr);
    levels = ubidi_getLevels(bidi, &errorCode);
    if (U_FAILURE(errorCode)) {
        strcpy(levelChars, "BAD LEVELS");
    } else {
        printf("Processed length: %d", length); fputs("\n", stderr);
        for (i = 0; i < length; i++) {
            lev = levels[i];
            if (lev < sizeof(columns)) {
                levelChars[i] = columns[lev];
            } else {
                levelChars[i] = '+';
            }
        }
        levelChars[length] = 0;
    }
    printf("Levels: %s", levelChars); fputs("\n", stderr);
    printf("Source: %s", src); fputs("\n", stderr);
    printf("Result: %s", dst); fputs("\n", stderr);
    printf("Direction: %d", ubidi_getDirection(bidi)); fputs("\n", stderr);
    printf("paraLevel: %d", ubidi_getParaLevel(bidi)); fputs("\n", stderr);
    i = ubidi_getReorderingMode(bidi);
    printf("reorderingMode: %d = %s", i, reorderingModeNames[i]);
    fputs("\n", stderr);
    i = ubidi_getReorderingOptions(bidi);
    printf("reorderingOptions: %d = %s", i, reorderingOptionNames(buffer, i));
    fputs("\n", stderr);
    runCount = ubidi_countRuns(bidi, &errorCode);
    if (U_FAILURE(errorCode)) {
        printf( "BAD RUNS");
    } else {
        printf("Runs: %d => logicalStart.length/level: ", runCount);
        for (i = 0; i < runCount; i++) {
            UBiDiDirection dir;
            int32_t start, len;
            dir = ubidi_getVisualRun(bidi, i, &start, &len);
            printf(" %d.%d/%d", start, len, dir);
        }
    }
    fputs("\n", stderr);
}

static void checkWhatYouCan(UBiDi *bidi, const char *srcChars, const char *dstChars) {
/* srcChars and dstChars are char arrays encoded as pseudo Bidi */
    int32_t i, idx, logLimit, visLimit;
    UBool testOK, errMap, errDst;
    UErrorCode errorCode=U_ZERO_ERROR;
    int32_t visMap[MAXLEN];
    int32_t logMap[MAXLEN];
    char accumSrc[MAXLEN];
    char accumDst[MAXLEN];
    ubidi_getVisualMap(bidi, visMap, &errorCode);
    ubidi_getLogicalMap(bidi, logMap, &errorCode);
	REQUIRE(U_SUCCESS(errorCode));

    testOK = true;
    errMap = errDst = false;
    logLimit = ubidi_getProcessedLength(bidi);
    visLimit = ubidi_getResultLength(bidi);
    memset(accumSrc, '?', logLimit);
    memset(accumDst, '?', visLimit);

    for (i = 0; i < logLimit; i++) {
        idx = ubidi_getVisualIndex(bidi, i, &errorCode);
        if (idx != logMap[i]) {
            errMap = true;
        }
        if (idx == UBIDI_MAP_NOWHERE) {
            continue;
        }
        if (idx >= visLimit) {
            continue;
        }
        accumDst[idx] = srcChars[i];
        if (!matchingPair(bidi, i, srcChars[i], dstChars[idx])) {
            errDst = true;
        }
    }
    accumDst[visLimit] = 0;

	REQUIRE(U_SUCCESS(errorCode));

    if (errMap) {
        if (testOK) {
            printCaseInfo(bidi, srcChars, dstChars);
            testOK = false;
        }
        printf("Mismatch between getLogicalMap() and getVisualIndex()\n");
        printf("Map    :");
        for (i = 0; i < logLimit; i++) {
            printf(" %d", logMap[i]);
        }
        fputs("\n", stderr);
        printf("Indexes:");
        for (i = 0; i < logLimit; i++) {
            printf(" %d", ubidi_getVisualIndex(bidi, i, &errorCode));
        }
        fputs("\n", stderr);
    }
    if (errDst) {
        if (testOK) {
            printCaseInfo(bidi, srcChars, dstChars);
            testOK = false;
        }
        printf("Source does not map to Result\n");
        printf("We got: %s", accumDst); fputs("\n", stderr);
    }

    errMap = errDst = false;
    for (i = 0; i < visLimit; i++) {
        idx = ubidi_getLogicalIndex(bidi, i, &errorCode);
        if (idx != visMap[i]) {
            errMap = true;
        }
        if (idx == UBIDI_MAP_NOWHERE) {
            continue;
        }
        if (idx >= logLimit) {
            continue;
        }
        accumSrc[idx] = dstChars[i];
        if (!matchingPair(bidi, idx, srcChars[idx], dstChars[i])) {
            errDst = true;
        }
    }
    accumSrc[logLimit] = 0;

	REQUIRE(U_SUCCESS(errorCode));

    if (errMap) {
        if (testOK) {
            printCaseInfo(bidi, srcChars, dstChars);
            testOK = false;
        }
        printf("Mismatch between getVisualMap() and getLogicalIndex()\n");
        printf("Map    :");
        for (i = 0; i < visLimit; i++) {
            printf(" %d", visMap[i]);
        }
        fputs("\n", stderr);
        printf("Indexes:");
        for (i = 0; i < visLimit; i++) {
            printf(" %d", ubidi_getLogicalIndex(bidi, i, &errorCode));
        }
        fputs("\n", stderr);
    }
    if (errDst) {
        if (testOK) {
            printCaseInfo(bidi, srcChars, dstChars);
            testOK = false;
        }
        printf("Result does not map to Source\n");
        printf("We got: %s", accumSrc);
        fputs("\n", stderr);
    }

    REQUIRE(testOK);
}


/* helpers ------------------------------------------------------------------ */

static void initCharFromDirProps(void) {
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

/* return a string with characters according to the desired directional properties */
static UChar *
getStringFromDirProps(const uint8_t *dirProps, int32_t length, UChar *buffer) {
    int32_t i;

    initCharFromDirProps();

    /* this part would have to be modified for UTF-x */
    for(i=0; i<length; ++i) {
        buffer[i]=charFromDirProp[dirProps[i]];
    }
    buffer[length]=0;
    return buffer;
}

static void assertRoundTrip(UBiDi *pBiDi, int32_t tc, int32_t outIndex, const char *srcChars,
                const char *destChars, const UChar *dest, int32_t destLen,
                int mode, int option, UBiDiLevel level) {
    static const char roundtrip[TC_COUNT][MODES_COUNT][OPTIONS_COUNT]
                [LEVELS_COUNT] = {
        { /* TC 0: 123 */
            {{ 1,  1}, { 1,  1}}, /* UBIDI_REORDER_GROUP_NUMBERS_WITH_R */
            {{ 1,  1}, { 1,  1}}, /* UBIDI_REORDER_INVERSE_LIKE_DIRECT */
            {{ 1,  1}, { 1,  1}}, /* UBIDI_REORDER_NUMBERS_SPECIAL */
            {{ 1,  1}, { 1,  1}}, /* UBIDI_REORDER_INVERSE_FOR_NUMBERS_SPECIAL */
            {{ 1,  1}, { 1,  1}}  /* UBIDI_REORDER_INVERSE_NUMBERS_AS_L */
        },
        { /* TC 1: .123->4.5 */
            {{ 1,  1}, { 1,  1}}, /* UBIDI_REORDER_GROUP_NUMBERS_WITH_R */
            {{ 1,  1}, { 1,  1}}, /* UBIDI_REORDER_INVERSE_LIKE_DIRECT */
            {{ 1,  1}, { 1,  1}}, /* UBIDI_REORDER_NUMBERS_SPECIAL */
            {{ 1,  1}, { 1,  1}}, /* UBIDI_REORDER_INVERSE_FOR_NUMBERS_SPECIAL */
            {{ 1,  1}, { 1,  1}}  /* UBIDI_REORDER_INVERSE_NUMBERS_AS_L */
        },
        { /* TC 2: 678 */
            {{ 1,  1}, { 1,  1}}, /* UBIDI_REORDER_GROUP_NUMBERS_WITH_R */
            {{ 1,  1}, { 1,  1}}, /* UBIDI_REORDER_INVERSE_LIKE_DIRECT */
            {{ 1,  1}, { 1,  1}}, /* UBIDI_REORDER_NUMBERS_SPECIAL */
            {{ 1,  1}, { 1,  1}}, /* UBIDI_REORDER_INVERSE_FOR_NUMBERS_SPECIAL */
            {{ 1,  1}, { 1,  1}}  /* UBIDI_REORDER_INVERSE_NUMBERS_AS_L */
        },
        { /* TC 3: .678->8.9 */
            {{ 1,  1}, { 1,  1}}, /* UBIDI_REORDER_GROUP_NUMBERS_WITH_R */
            {{ 1,  1}, { 1,  1}}, /* UBIDI_REORDER_INVERSE_LIKE_DIRECT */
            {{ 1,  1}, { 1,  1}}, /* UBIDI_REORDER_NUMBERS_SPECIAL */
            {{ 1,  1}, { 1,  1}}, /* UBIDI_REORDER_INVERSE_FOR_NUMBERS_SPECIAL */
            {{ 0,  0}, { 1,  1}}  /* UBIDI_REORDER_INVERSE_NUMBERS_AS_L */
        },
        { /* TC 4: MLK1.2,3JIH */
            {{ 1,  1}, { 1,  1}}, /* UBIDI_REORDER_GROUP_NUMBERS_WITH_R */
            {{ 1,  1}, { 1,  1}}, /* UBIDI_REORDER_INVERSE_LIKE_DIRECT */
            {{ 1,  1}, { 1,  1}}, /* UBIDI_REORDER_NUMBERS_SPECIAL */
            {{ 1,  1}, { 1,  1}}, /* UBIDI_REORDER_INVERSE_FOR_NUMBERS_SPECIAL */
            {{ 1,  1}, { 1,  1}}  /* UBIDI_REORDER_INVERSE_NUMBERS_AS_L */
        },
        { /* TC 5: FE.>12-> */
            {{ 1,  1}, { 1,  1}}, /* UBIDI_REORDER_GROUP_NUMBERS_WITH_R */
            {{ 1,  1}, { 1,  1}}, /* UBIDI_REORDER_INVERSE_LIKE_DIRECT */
            {{ 0,  1}, { 1,  1}}, /* UBIDI_REORDER_NUMBERS_SPECIAL */
            {{ 1,  1}, { 1,  1}}, /* UBIDI_REORDER_INVERSE_FOR_NUMBERS_SPECIAL */
            {{ 1,  1}, { 1,  1}}  /* UBIDI_REORDER_INVERSE_NUMBERS_AS_L */
        },
        { /* TC 6: JIH.>12->a */
            {{ 1,  1}, { 1,  1}}, /* UBIDI_REORDER_GROUP_NUMBERS_WITH_R */
            {{ 1,  1}, { 1,  1}}, /* UBIDI_REORDER_INVERSE_LIKE_DIRECT */
            {{ 0,  0}, { 1,  1}}, /* UBIDI_REORDER_NUMBERS_SPECIAL */
            {{ 1,  1}, { 1,  1}}, /* UBIDI_REORDER_INVERSE_FOR_NUMBERS_SPECIAL */
            {{ 1,  1}, { 1,  1}}  /* UBIDI_REORDER_INVERSE_NUMBERS_AS_L */
        },
        { /* TC 7: CBA.>67->89=a */
            {{ 1,  1}, { 1,  1}}, /* UBIDI_REORDER_GROUP_NUMBERS_WITH_R */
            {{ 1,  1}, { 1,  1}}, /* UBIDI_REORDER_INVERSE_LIKE_DIRECT */
            {{ 0,  1}, { 1,  1}}, /* UBIDI_REORDER_NUMBERS_SPECIAL */
            {{ 1,  1}, { 1,  1}}, /* UBIDI_REORDER_INVERSE_FOR_NUMBERS_SPECIAL */
            {{ 0,  0}, { 1,  1}}  /* UBIDI_REORDER_INVERSE_NUMBERS_AS_L */
        },
        { /* TC 8: CBA.>123->xyz */
            {{ 1,  1}, { 1,  1}}, /* UBIDI_REORDER_GROUP_NUMBERS_WITH_R */
            {{ 1,  1}, { 1,  1}}, /* UBIDI_REORDER_INVERSE_LIKE_DIRECT */
            {{ 0,  0}, { 1,  1}}, /* UBIDI_REORDER_NUMBERS_SPECIAL */
            {{ 1,  1}, { 1,  1}}, /* UBIDI_REORDER_INVERSE_FOR_NUMBERS_SPECIAL */
            {{ 1,  1}, { 1,  1}}  /* UBIDI_REORDER_INVERSE_NUMBERS_AS_L */
        },
        { /* TC 9: .>12->xyz */
            {{ 1,  1}, { 1,  1}}, /* UBIDI_REORDER_GROUP_NUMBERS_WITH_R */
            {{ 1,  1}, { 1,  1}}, /* UBIDI_REORDER_INVERSE_LIKE_DIRECT */
            {{ 1,  0}, { 1,  1}}, /* UBIDI_REORDER_NUMBERS_SPECIAL */
            {{ 1,  1}, { 1,  1}}, /* UBIDI_REORDER_INVERSE_FOR_NUMBERS_SPECIAL */
            {{ 1,  1}, { 1,  1}}  /* UBIDI_REORDER_INVERSE_NUMBERS_AS_L */
        },
        { /* TC 10: a.>67->xyz */
            {{ 1,  1}, { 1,  1}}, /* UBIDI_REORDER_GROUP_NUMBERS_WITH_R */
            {{ 1,  1}, { 1,  1}}, /* UBIDI_REORDER_INVERSE_LIKE_DIRECT */
            {{ 1,  1}, { 1,  1}}, /* UBIDI_REORDER_NUMBERS_SPECIAL */
            {{ 1,  1}, { 1,  1}}, /* UBIDI_REORDER_INVERSE_FOR_NUMBERS_SPECIAL */
            {{ 1,  0}, { 1,  1}}  /* UBIDI_REORDER_INVERSE_NUMBERS_AS_L */
        },
        { /* TC 11: 123JIH */
            {{ 1,  1}, { 1,  1}}, /* UBIDI_REORDER_GROUP_NUMBERS_WITH_R */
            {{ 1,  1}, { 1,  1}}, /* UBIDI_REORDER_INVERSE_LIKE_DIRECT */
            {{ 1,  1}, { 1,  1}}, /* UBIDI_REORDER_NUMBERS_SPECIAL */
            {{ 1,  1}, { 1,  1}}, /* UBIDI_REORDER_INVERSE_FOR_NUMBERS_SPECIAL */
            {{ 1,  1}, { 1,  1}}  /* UBIDI_REORDER_INVERSE_NUMBERS_AS_L */
        },
        { /* TC 12: 123 JIH */
            {{ 1,  1}, { 1,  1}}, /* UBIDI_REORDER_GROUP_NUMBERS_WITH_R */
            {{ 1,  1}, { 1,  1}}, /* UBIDI_REORDER_INVERSE_LIKE_DIRECT */
            {{ 1,  1}, { 1,  1}}, /* UBIDI_REORDER_NUMBERS_SPECIAL */
            {{ 1,  1}, { 1,  1}}, /* UBIDI_REORDER_INVERSE_FOR_NUMBERS_SPECIAL */
            {{ 1,  1}, { 1,  1}}  /* UBIDI_REORDER_INVERSE_NUMBERS_AS_L */
        }
    };

    #define SET_ROUND_TRIP_MODE(mode) \
        ubidi_setReorderingMode(pBiDi, mode); \
        desc = #mode; \
        break;

    UErrorCode rc = U_ZERO_ERROR;
    UChar dest2[MAXLEN];
    int32_t destLen2;
    const char* desc;
    char destChars2[MAXLEN];
    char destChars3[MAXLEN];

    switch (modes[mode].value) {
        case UBIDI_REORDER_NUMBERS_SPECIAL:
            SET_ROUND_TRIP_MODE(UBIDI_REORDER_INVERSE_FOR_NUMBERS_SPECIAL)
        case UBIDI_REORDER_GROUP_NUMBERS_WITH_R:
            SET_ROUND_TRIP_MODE(UBIDI_REORDER_GROUP_NUMBERS_WITH_R)
        case UBIDI_REORDER_RUNS_ONLY:
            SET_ROUND_TRIP_MODE(UBIDI_REORDER_RUNS_ONLY)
        case UBIDI_REORDER_INVERSE_NUMBERS_AS_L:
            SET_ROUND_TRIP_MODE(UBIDI_REORDER_DEFAULT)
        case UBIDI_REORDER_INVERSE_LIKE_DIRECT:
            SET_ROUND_TRIP_MODE(UBIDI_REORDER_DEFAULT)
        case UBIDI_REORDER_INVERSE_FOR_NUMBERS_SPECIAL:
            SET_ROUND_TRIP_MODE(UBIDI_REORDER_NUMBERS_SPECIAL)
        default:
            SET_ROUND_TRIP_MODE(UBIDI_REORDER_INVERSE_LIKE_DIRECT)
    }
    ubidi_setReorderingOptions(pBiDi, UBIDI_OPTION_REMOVE_CONTROLS);

    ubidi_setPara(pBiDi, dest, destLen, level, NULL, &rc);
	REQUIRE(U_SUCCESS(rc));
    *dest2 = 0;
    destLen2 = ubidi_writeReordered(pBiDi, dest2, MAXLEN, UBIDI_DO_MIRRORING, &rc);
	REQUIRE(U_SUCCESS(rc));

    u16ToPseudo(destLen, dest, destChars3);
    u16ToPseudo(destLen2, dest2, destChars2);
    checkWhatYouCan(pBiDi, destChars3, destChars2);
	if (strcmp(srcChars, destChars2)) {
		REQUIRE(!roundtrip[tc][mode][option][level]);
	}

    checkResultLength(pBiDi, destChars, destChars2, destLen2, desc, "UBIDI_OPTION_REMOVE_CONTROLS", level);
	if (outIndex > -1) {
		checkMaps(pBiDi, outIndex, srcChars, destChars, desc, "UBIDI_OPTION_REMOVE_CONTROLS", level, false);
	}
}

static void checkResultLength(UBiDi *pBiDi, const char *srcChars, const char *destChars, int32_t destLen,
		const char* mode, const char* option, UBiDiLevel level) {
    int32_t actualLen;
    if (strcmp(mode, "UBIDI_REORDER_INVERSE_NUMBERS_AS_L") == 0)
        actualLen = (int32_t)strlen(destChars);
    else
        actualLen = ubidi_getResultLength(pBiDi);

	REQUIRE(actualLen == destLen);
}

static char * formatMap(const int32_t * map, int len, char * buffer)
{
    int32_t i, k;
    char c;
    for (i = 0; i < len; i++) {
        k = map[i];
        if (k < 0)
            c = '-';
        else if (k >= (int32_t)sizeof(columns))
            c = '+';
        else
            c = columns[k];
        buffer[i] = c;
    }
    buffer[len] = '\0';
    return buffer;
}

static void checkMaps(UBiDi *pBiDi, int32_t stringIndex, const char *src, const char *dest, const char *mode,
		const char* option, UBiDiLevel level, UBool forward) {
    int32_t actualLogicalMap[MAX_MAP_LENGTH];
    int32_t actualVisualMap[MAX_MAP_LENGTH];
    int32_t getIndexMap[MAX_MAP_LENGTH];
    int32_t i, srcLen, resLen, idx;
    const int32_t *expectedLogicalMap, *expectedVisualMap;
    UErrorCode rc = U_ZERO_ERROR;

    if (forward) {
        expectedLogicalMap = forwardMap[stringIndex];
        expectedVisualMap  = inverseMap[stringIndex];
    }
    else {
        expectedLogicalMap = inverseMap[stringIndex];
        expectedVisualMap  = forwardMap[stringIndex];
    }
    ubidi_getLogicalMap(pBiDi, actualLogicalMap, &rc);
	REQUIRE(U_SUCCESS(rc));

    srcLen = ubidi_getProcessedLength(pBiDi);
    REQUIRE(memcmp(expectedLogicalMap, actualLogicalMap, srcLen * sizeof(int32_t)) == 0);
    resLen = ubidi_getResultLength(pBiDi);
    ubidi_getVisualMap(pBiDi, actualVisualMap, &rc);
	REQUIRE(U_SUCCESS(rc));

    REQUIRE(memcmp(expectedVisualMap, actualVisualMap, resLen * sizeof(int32_t)) == 0);

    for (i = 0; i < srcLen; i++) {
        idx = ubidi_getVisualIndex(pBiDi, i, &rc);
		CHECK(U_SUCCESS(rc));
        getIndexMap[i] = idx;
    }
    REQUIRE(memcmp(actualLogicalMap, getIndexMap, srcLen * sizeof(int32_t)) == 0);

    for (i = 0; i < resLen; i++) {
        idx = ubidi_getLogicalIndex(pBiDi, i, &rc);
		CHECK(U_SUCCESS(rc));
        getIndexMap[i] = idx;
    }
    REQUIRE(memcmp(actualVisualMap, getIndexMap, resLen * sizeof(int32_t)) == 0);
}

static void _testInverseBidi(UBiDi *pBiDi, const UChar *src, int32_t srcLength, UBiDiLevel direction,
		UErrorCode *pErrorCode) {
    UChar visualLTR[MAXLEN], logicalDest[MAXLEN], visualDest[MAXLEN];
    int32_t ltrLength, logicalLength, visualLength;

    if(direction==0) {
        /* convert visual to logical */
        ubidi_setInverse(pBiDi, true);
		CHECK(ubidi_isInverse(pBiDi));
        ubidi_setPara(pBiDi, src, srcLength, 0, NULL, pErrorCode);
		CHECK(src == ubidi_getText(pBiDi));
        logicalLength=ubidi_writeReordered(pBiDi, logicalDest, UPRV_LENGTHOF(logicalDest),
                                           UBIDI_DO_MIRRORING|UBIDI_INSERT_LRM_FOR_NUMERIC, pErrorCode);

        /* convert back to visual LTR */
        ubidi_setInverse(pBiDi, false);
		CHECK(!ubidi_isInverse(pBiDi));
        ubidi_setPara(pBiDi, logicalDest, logicalLength, 0, NULL, pErrorCode);
        visualLength=ubidi_writeReordered(pBiDi, visualDest, UPRV_LENGTHOF(visualDest),
                                          UBIDI_DO_MIRRORING|UBIDI_REMOVE_BIDI_CONTROLS, pErrorCode);
    } else {
        /* reverse visual from RTL to LTR */
        ltrLength=ubidi_writeReverse(src, srcLength, visualLTR, UPRV_LENGTHOF(visualLTR), 0, pErrorCode);

        /* convert visual RTL to logical */
        ubidi_setInverse(pBiDi, true);
        ubidi_setPara(pBiDi, visualLTR, ltrLength, 0, NULL, pErrorCode);
        logicalLength=ubidi_writeReordered(pBiDi, logicalDest, UPRV_LENGTHOF(logicalDest),
                                           UBIDI_DO_MIRRORING|UBIDI_INSERT_LRM_FOR_NUMERIC, pErrorCode);

        /* convert back to visual RTL */
        ubidi_setInverse(pBiDi, false);
        ubidi_setPara(pBiDi, logicalDest, logicalLength, 0, NULL, pErrorCode);
        visualLength=ubidi_writeReordered(pBiDi, visualDest, UPRV_LENGTHOF(visualDest),
                                          UBIDI_DO_MIRRORING|UBIDI_REMOVE_BIDI_CONTROLS|UBIDI_OUTPUT_REVERSE, pErrorCode);
    }

    /* check and print results */
	REQUIRE(U_SUCCESS(*pErrorCode));

    if (srcLength==visualLength && memcmp(src, visualDest, srcLength*U_SIZEOF_UCHAR)==0) {
        ++countRoundtrips;
    } else {
        ++countNonRoundtrips;
		CHECK(false);
    }
}

#define COUNT_REPEAT_SEGMENTS 6

static const UChar repeatSegments[COUNT_REPEAT_SEGMENTS][2]={
    { 0x61, 0x62 },     /* L */
    { 0x5d0, 0x5d1 },   /* R */
    { 0x627, 0x628 },   /* AL */
    { 0x31, 0x32 },     /* EN */
    { 0x661, 0x662 },   /* AN */
    { 0x20, 0x20 }      /* WS (N) */
};

static void _testManyInverseBidi(UBiDi *pBiDi, UBiDiLevel direction) {
    UChar text[8]={ 0, 0, 0x20, 0, 0, 0x20, 0, 0 };
    int i, j, k;
    UErrorCode errorCode;

    for(i=0; i<COUNT_REPEAT_SEGMENTS; ++i) {
        text[0]=repeatSegments[i][0];
        text[1]=repeatSegments[i][1];
        for(j=0; j<COUNT_REPEAT_SEGMENTS; ++j) {
            text[3]=repeatSegments[j][0];
            text[4]=repeatSegments[j][1];
            for(k=0; k<COUNT_REPEAT_SEGMENTS; ++k) {
                text[6]=repeatSegments[k][0];
                text[7]=repeatSegments[k][1];

                errorCode=U_ZERO_ERROR;
                _testInverseBidi(pBiDi, text, 8, direction, &errorCode);
            }
        }
    }
}

static void _testWriteReverse() {
    /* U+064e and U+0650 are combining marks (Mn) */
    static const UChar forward[]={
        0x200f, 0x627, 0x64e, 0x650, 0x20, 0x28, 0x31, 0x29
    }, reverseKeepCombining[]={
        0x29, 0x31, 0x28, 0x20, 0x627, 0x64e, 0x650, 0x200f
    }, reverseRemoveControlsKeepCombiningDoMirror[]={
        0x28, 0x31, 0x29, 0x20, 0x627, 0x64e, 0x650
    };
    UChar reverse[10];
    UErrorCode errorCode;
    int32_t length;

    /* test ubidi_writeReverse() with "interesting" options */
    errorCode=U_ZERO_ERROR;
    length=ubidi_writeReverse(forward, UPRV_LENGTHOF(forward),
                              reverse, UPRV_LENGTHOF(reverse),
                              UBIDI_KEEP_BASE_COMBINING,
                              &errorCode);
	CHECK(U_SUCCESS(errorCode));
	CHECK(length == UPRV_LENGTHOF(reverseKeepCombining));
	CHECK(memcmp(reverse, reverseKeepCombining, length*U_SIZEOF_UCHAR) == 0);

    memset(reverse, 0xa5, UPRV_LENGTHOF(reverse)*U_SIZEOF_UCHAR);
    errorCode=U_ZERO_ERROR;
    length=ubidi_writeReverse(forward, UPRV_LENGTHOF(forward),
                              reverse, UPRV_LENGTHOF(reverse),
                              UBIDI_REMOVE_BIDI_CONTROLS|UBIDI_DO_MIRRORING|UBIDI_KEEP_BASE_COMBINING,
                              &errorCode);
	CHECK(U_SUCCESS(errorCode));
	CHECK(length == UPRV_LENGTHOF(reverseRemoveControlsKeepCombiningDoMirror));
	CHECK(memcmp(reverse, reverseRemoveControlsKeepCombiningDoMirror, length*U_SIZEOF_UCHAR) == 0);
}

static void _testManyAddedPoints(void) {
    UErrorCode errorCode = U_ZERO_ERROR;
    UBiDi *bidi = ubidi_open();
    UChar text[90], dest[MAXLEN], expected[120];
    int destLen, i;
    for (i = 0; i < UPRV_LENGTHOF(text); i+=3) {
        text[i] = 0x0061; /* 'a' */
        text[i+1] = 0x05d0;
        text[i+2] = 0x0033; /* '3' */
    }
    ubidi_setReorderingMode(bidi, UBIDI_REORDER_INVERSE_LIKE_DIRECT);
    ubidi_setReorderingOptions(bidi, UBIDI_OPTION_INSERT_MARKS);
    ubidi_setPara(bidi, text, UPRV_LENGTHOF(text), UBIDI_LTR, NULL, &errorCode);
    destLen = ubidi_writeReordered(bidi, dest, MAXLEN, 0, &errorCode);
    for (i = 0; i < UPRV_LENGTHOF(expected); i+=4) {
        expected[i] = 0x0061; /* 'a' */
        expected[i+1] = 0x05d0;
        expected[i+2] = 0x200e;
        expected[i+3] = 0x0033; /* '3' */
    }
    REQUIRE(memcmp(dest, expected, destLen * sizeof(UChar)) == 0);
    ubidi_close(bidi);
}

static void _testMisc() {
    UErrorCode errorCode = U_ZERO_ERROR;
    UBiDi *bidi = ubidi_open();
    UChar src[3], dest[MAXLEN], expected[5];
    int destLen;
    ubidi_setInverse(bidi, true);
    src[0] = src[1] = src[2] = 0x0020;
    ubidi_setPara(bidi, src, UPRV_LENGTHOF(src), UBIDI_RTL, NULL, &errorCode);
    destLen = ubidi_writeReordered(bidi, dest, MAXLEN,
              UBIDI_OUTPUT_REVERSE | UBIDI_INSERT_LRM_FOR_NUMERIC,
              &errorCode);
    u_unescape("\\u200f   \\u200f", expected, 5);
    REQUIRE(memcmp(dest, expected, destLen * sizeof(UChar)) == 0);
    ubidi_close(bidi);
}

static const char* inverseBasic(UBiDi *pBiDi, const char *srcChars, int32_t srcLen, uint32_t option,
		UBiDiLevel level, char *result) {
    UErrorCode rc = U_ZERO_ERROR;
    int32_t destLen;
    UChar src[MAXLEN], dest2[MAXLEN];

    if (pBiDi == NULL || srcChars == NULL) {
        return NULL;
    }
    ubidi_setReorderingOptions(pBiDi, option);
    pseudoToU16(srcLen, srcChars, src);
    ubidi_setPara(pBiDi, src, srcLen, level, NULL, &rc);
	REQUIRE(U_SUCCESS(rc));

    *dest2 = 0;
    destLen = ubidi_writeReordered(pBiDi, dest2, MAXLEN, UBIDI_DO_MIRRORING, &rc);
	REQUIRE(U_SUCCESS(rc));
    u16ToPseudo(destLen, dest2, result);
    if (!(option == UBIDI_OPTION_INSERT_MARKS)) {
        checkWhatYouCan(pBiDi, srcChars, result);
    }
    return result;
}

static void verifyCallbackParams(UBiDiClassCallback* fn, const void* context, UBiDiClassCallback* expectedFn,
		const void* expectedContext, int32_t sizeOfContext) {
	REQUIRE(fn == expectedFn);
	REQUIRE(context == expectedContext);
	if (context) {
		REQUIRE(memcmp(context, expectedContext, sizeOfContext) == 0);
	}
}

static char *aescstrdup(const UChar* unichars,int32_t length) {
    char *newString,*targetLimit,*target;
    UConverterFromUCallback cb;
    const void *p;
    UErrorCode errorCode = U_ZERO_ERROR;
#if U_CHARSET_FAMILY==U_EBCDIC_FAMILY
#   if U_PLATFORM == U_PF_OS390
        static const char convName[] = "ibm-1047";
#   else
        static const char convName[] = "ibm-37";
#   endif
#else
    static const char convName[] = "US-ASCII";
#endif
    UConverter* conv = ucnv_open(convName, &errorCode);
    if(length==-1){
        length = u_strlen( unichars);
    }
    newString = (char*)malloc ( sizeof(char) * 8 * (length +1));
    target = newString;
    targetLimit = newString+sizeof(char) * 8 * (length +1);
    ucnv_setFromUCallBack(conv, UCNV_FROM_U_CALLBACK_ESCAPE, UCNV_ESCAPE_C, &cb, &p, &errorCode);
    ucnv_fromUnicode(conv,&target,targetLimit, &unichars, (UChar*)(unichars+length),NULL,true,&errorCode);
    ucnv_close(conv);
    *target = '\0';
    return newString;
}

