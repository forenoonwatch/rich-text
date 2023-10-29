#pragma once

#include <ubidiimp.h>

struct U8BiDi {
	/* pointer to parent paragraph object (pointer to self if this object is
	 * a paragraph object); set to NULL in a newly opened object; set to a
	 * real value after a successful execution of ubidi_setPara or ubidi_setLine
	 */
	const U8BiDi * pParaBiDi;

	/* alias pointer to the current text */
	const char *text;

	/* length of the current text */
	int32_t originalLength;

	/* if the UBIDI_OPTION_STREAMING option is set, this is the length
	 * of text actually processed by ubidi_setPara, which may be shorter than
	 * the original length.
	 * Otherwise, it is identical to the original length.
	 */
	int32_t length;

	/* if the UBIDI_OPTION_REMOVE_CONTROLS option is set, and/or
	 * marks are allowed to be inserted in one of the reordering mode, the
	 * length of the result string may be different from the processed length.
	 */
	int32_t resultLength;

	/* memory sizes in bytes */
	int32_t dirPropsSize, levelsSize, openingsSize, parasSize, runsSize, isolatesSize;

	/* allocated memory */
	DirProp *dirPropsMemory;
	UBiDiLevel *levelsMemory;
	Opening *openingsMemory;
	Para *parasMemory;
	Run *runsMemory;
	Isolate *isolatesMemory;

	/* indicators for whether memory may be allocated after ubidi_open() */
	UBool mayAllocateText, mayAllocateRuns;

	/* arrays with one value per text-character */
	DirProp *dirProps;
	UBiDiLevel *levels;

	/* are we performing an approximation of the "inverse BiDi" algorithm? */
	UBool isInverse;

	/* are we using the basic algorithm or its variation? */
	UBiDiReorderingMode reorderingMode;

	/* UBIDI_REORDER_xxx values must be ordered so that all the regular
	 * logical to visual modes come first, and all inverse BiDi modes
	 * come last.
	 */
	#define UBIDI_REORDER_LAST_LOGICAL_TO_VISUAL	UBIDI_REORDER_NUMBERS_SPECIAL

	/* bitmask for reordering options */
	uint32_t reorderingOptions;

	/* must block separators receive level 0? */
	UBool orderParagraphsLTR;

	/* the paragraph level */
	UBiDiLevel paraLevel;
	/* original paraLevel when contextual */
	/* must be one of UBIDI_DEFAULT_xxx or 0 if not contextual */
	UBiDiLevel defaultParaLevel;

	/* context data */
	const char *prologue;
	int32_t proLength;
	const char *epilogue;
	int32_t epiLength;

	/* the following is set in ubidi_setPara, used in processPropertySeq */
	const struct ImpTabPair * pImpTabPair;  /* pointer to levels state table pair */

	/* the overall paragraph or line directionality - see UBiDiDirection */
	UBiDiDirection direction;

	/* flags is a bit set for which directional properties are in the text */
	Flags flags;

	/* lastArabicPos is index to the last AL in the text, -1 if none */
	int32_t lastArabicPos;

	/* characters after trailingWSStart are WS and are */
	/* implicitly at the paraLevel (rule (L1)) - levels may not reflect that */
	int32_t trailingWSStart;

	/* fields for paragraph handling */
	int32_t paraCount;				  /* set in getDirProps() */
	/* filled in getDirProps() */
	Para *paras;

	/* for relatively short text, we only need a tiny array of paras (no malloc()) */
	Para simpleParas[SIMPLE_PARAS_COUNT];

	/* fields for line reordering */
	int32_t runCount;	 /* ==-1: runs not set up yet */
	Run *runs;

	/* for non-mixed text, we only need a tiny array of runs (no malloc()) */
	Run simpleRuns[1];

	/* maximum or current nesting depth of isolate sequences */
	/* Within resolveExplicitLevels() and checkExplicitLevels(), this is the maximal
	   nesting encountered.
	   Within resolveImplicitLevels(), this is the index of the current isolates
	   stack entry. */
	int32_t isolateCount;
	Isolate *isolates;

	/* for simple text, have a small stack (no malloc()) */
	Isolate simpleIsolates[SIMPLE_ISOLATES_COUNT];

	/* for inverse Bidi with insertion of directional marks */
	InsertPoints insertPoints;

	/* for option UBIDI_OPTION_REMOVE_CONTROLS */
	int32_t controlCount;

	/* for Bidi class callback */
	UBiDiClassCallback *fnClassCallback;	/* action pointer */
	const void *coClassCallback;			/* context pointer */
};

UBiDiLevel get_para_level_internal(const U8BiDi* pBiDi, int32_t index);

