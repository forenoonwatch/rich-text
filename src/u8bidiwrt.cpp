#include "u8bidi.hpp"
#include "u8bidi_impl.hpp"

#include <unicode/utf8.h>
#include <ustr_imp.h>

#define IS_COMBINING(type) ((1UL<<(type))&(1UL<<U_NON_SPACING_MARK|1UL<<U_COMBINING_SPACING_MARK|1UL<<U_ENCLOSING_MARK))

/*
 * When we have UBIDI_OUTPUT_REVERSE set on ubidi_writeReordered(), then we
 * semantically write RTL runs in reverse and later reverse them again.
 * Instead, we actually write them in forward order to begin with.
 * However, if the RTL run was to be mirrored, we need to mirror here now
 * since the implicit second reversal must not do it.
 * It looks strange to do mirroring in LTR output, but it is only because
 * we are writing RTL output in reverse.
 */
static int32_t doWriteForward(const char *src, int32_t srcLength, char *dest, int32_t destSize, uint16_t options,
		UErrorCode *pErrorCode);

static int32_t doWriteReverse(const char *src, int32_t srcLength, char *dest, int32_t destSize, uint16_t options,
		UErrorCode *pErrorCode);

// Public Functions

int32_t u8bidi_write_reordered(U8BiDi* pBiDi, char* dest, int32_t destSize, uint16_t options,
		UErrorCode* pErrorCode) {
	const char *text;
    char *saveDest;
    int32_t length, destCapacity;
    int32_t run, runCount, logicalStart, runLength;

    if(pErrorCode==nullptr || U_FAILURE(*pErrorCode)) {
        return 0;
    }

    /* more error checking */
    if( pBiDi==nullptr ||
        (text=pBiDi->text)==nullptr || (length=pBiDi->length)<0 ||
        destSize<0 || (destSize>0 && dest==nullptr))
    {
        *pErrorCode=U_ILLEGAL_ARGUMENT_ERROR;
        return 0;
    }

    /* do input and output overlap? */
    if( dest!=nullptr &&
        ((text>=dest && text<dest+destSize) ||
         (dest>=text && dest<text+pBiDi->originalLength)))
    {
        *pErrorCode=U_ILLEGAL_ARGUMENT_ERROR;
        return 0;
    }

    if(length==0) {
        /* nothing to do */
        return u_terminateChars(dest, destSize, 0, pErrorCode);
    }

    runCount=u8bidi_count_runs(pBiDi, pErrorCode);
    if(U_FAILURE(*pErrorCode)) {
        return 0;
    }

    /* destSize shrinks, later destination length=destCapacity-destSize */
    saveDest=dest;
    destCapacity=destSize;

    /*
     * Option "insert marks" implies UBIDI_INSERT_LRM_FOR_NUMERIC if the
     * reordering mode (checked below) is appropriate.
     */
    if(pBiDi->reorderingOptions & UBIDI_OPTION_INSERT_MARKS) {
        options|=UBIDI_INSERT_LRM_FOR_NUMERIC;
        options&=~UBIDI_REMOVE_BIDI_CONTROLS;
    }
    /*
     * Option "remove controls" implies UBIDI_REMOVE_BIDI_CONTROLS
     * and cancels UBIDI_INSERT_LRM_FOR_NUMERIC.
     */
    if(pBiDi->reorderingOptions & UBIDI_OPTION_REMOVE_CONTROLS) {
        options|=UBIDI_REMOVE_BIDI_CONTROLS;
        options&=~UBIDI_INSERT_LRM_FOR_NUMERIC;
    }
    /*
     * If we do not perform the "inverse BiDi" algorithm, then we
     * don't need to insert any LRMs, and don't need to test for it.
     */
    if((pBiDi->reorderingMode != UBIDI_REORDER_INVERSE_NUMBERS_AS_L) &&
       (pBiDi->reorderingMode != UBIDI_REORDER_INVERSE_LIKE_DIRECT)  &&
       (pBiDi->reorderingMode != UBIDI_REORDER_INVERSE_FOR_NUMBERS_SPECIAL) &&
       (pBiDi->reorderingMode != UBIDI_REORDER_RUNS_ONLY)) {
        options&=~UBIDI_INSERT_LRM_FOR_NUMERIC;
    }
    /*
     * Iterate through all visual runs and copy the run text segments to
     * the destination, according to the options.
     *
     * The tests for where to insert LRMs ignore the fact that there may be
     * BN codes or non-BMP code points at the beginning and end of a run;
     * they may insert LRMs unnecessarily but the tests are faster this way
     * (this would have to be improved for UTF-8).
     *
     * Note that the only errors that are set by doWriteXY() are buffer overflow
     * errors. Ignore them until the end, and continue for preflighting.
     */
    if(!(options&UBIDI_OUTPUT_REVERSE)) {
        /* forward output */
        if(!(options&UBIDI_INSERT_LRM_FOR_NUMERIC)) {
            /* do not insert BiDi controls */
            for(run=0; run<runCount; ++run) {
                if(UBIDI_LTR==u8bidi_get_visual_run(pBiDi, run, &logicalStart, &runLength)) {
                    runLength=doWriteForward(text+logicalStart, runLength, dest, destSize,
                                             (uint16_t)(options&~UBIDI_DO_MIRRORING), pErrorCode);
                } else {
                    runLength=doWriteReverse(text+logicalStart, runLength,
                                             dest, destSize,
                                             options, pErrorCode);
                }
                if(dest!=nullptr) {
                  dest+=runLength;
                }
                destSize-=runLength;
            }
        } else {
            /* insert BiDi controls for "inverse BiDi" */
            const DirProp *dirProps=pBiDi->dirProps;
            const char *src;
            UChar32 uc;
            UBiDiDirection dir;
            int32_t markFlag;

            for(run=0; run<runCount; ++run) {
                dir=u8bidi_get_visual_run(pBiDi, run, &logicalStart, &runLength);
                src=text+logicalStart;
                /* check if something relevant in insertPoints */
                markFlag=pBiDi->runs[run].insertRemove;
                if(markFlag<0) {        /* BiDi controls count */
                    markFlag=0;
                }

                if(UBIDI_LTR==dir) {
                    if((pBiDi->isInverse) &&
                       (/*run>0 &&*/ dirProps[logicalStart]!=L)) {
                        markFlag |= LRM_BEFORE;
                    }
                    if (markFlag & LRM_BEFORE) {
                        uc=LRM_CHAR;
                    }
                    else if (markFlag & RLM_BEFORE) {
                        uc=RLM_CHAR;
                    }
                    else  uc=0;
                    if(uc) {
                        if(destSize>0) {
							int32_t idx{};
							U8_APPEND_UNSAFE(dest, idx, uc);
							dest += idx;
                        }
                        --destSize;
                    }

                    runLength=doWriteForward(src, runLength,
                                             dest, destSize,
                                             (uint16_t)(options&~UBIDI_DO_MIRRORING), pErrorCode);
                    if(dest!=nullptr) {
                      dest+=runLength;
                    }
                    destSize-=runLength;

                    if((pBiDi->isInverse) &&
                       (/*run<runCount-1 &&*/ dirProps[logicalStart+runLength-1]!=L)) {
                        markFlag |= LRM_AFTER;
                    }
                    if (markFlag & LRM_AFTER) {
                        uc=LRM_CHAR;
                    }
                    else if (markFlag & RLM_AFTER) {
                        uc=RLM_CHAR;
                    }
                    else  uc=0;
                    if(uc) {
                        if(destSize>0) {
							int32_t idx{};
							U8_APPEND_UNSAFE(dest, idx, uc);
							dest += idx;
                        }
                        --destSize;
                    }
                } else {                /* RTL run */
                    if((pBiDi->isInverse) &&
                       (/*run>0 &&*/ !(MASK_R_AL&DIRPROP_FLAG(dirProps[logicalStart+runLength-1])))) {
                        markFlag |= RLM_BEFORE;
                    }
                    if (markFlag & LRM_BEFORE) {
                        uc=LRM_CHAR;
                    }
                    else if (markFlag & RLM_BEFORE) {
                        uc=RLM_CHAR;
                    }
                    else  uc=0;
                    if(uc) {
                        if(destSize>0) {
                            *dest++=uc;
                        }
                        --destSize;
                    }

                    runLength=doWriteReverse(src, runLength,
                                             dest, destSize,
                                             options, pErrorCode);
                    if(dest!=nullptr) {
                      dest+=runLength;
                    }
                    destSize-=runLength;

                    if((pBiDi->isInverse) &&
                       (/*run<runCount-1 &&*/ !(MASK_R_AL&DIRPROP_FLAG(dirProps[logicalStart])))) {
                        markFlag |= RLM_AFTER;
                    }
                    if (markFlag & LRM_AFTER) {
                        uc=LRM_CHAR;
                    }
                    else if (markFlag & RLM_AFTER) {
                        uc=RLM_CHAR;
                    }
                    else  uc=0;
                    if(uc) {
                        if(destSize>0) {
							int32_t idx{};
							U8_APPEND_UNSAFE(dest, idx, uc);
							dest += idx;
                        }
                        --destSize;
                    }
                }
            }
        }
    } else {
        /* reverse output */
        if(!(options&UBIDI_INSERT_LRM_FOR_NUMERIC)) {
            /* do not insert BiDi controls */
            for(run=runCount; --run>=0;) {
                if(UBIDI_LTR==u8bidi_get_visual_run(pBiDi, run, &logicalStart, &runLength)) {
                    runLength=doWriteReverse(text+logicalStart, runLength,
                                             dest, destSize,
                                             (uint16_t)(options&~UBIDI_DO_MIRRORING), pErrorCode);
                } else {
                    runLength=doWriteForward(text+logicalStart, runLength,
                                             dest, destSize,
                                             options, pErrorCode);
                }
                if(dest!=nullptr) {
                  dest+=runLength;
                }
                destSize-=runLength;
            }
        } else {
            /* insert BiDi controls for "inverse BiDi" */
            const DirProp *dirProps=pBiDi->dirProps;
            const char *src;
            UBiDiDirection dir;

            for(run=runCount; --run>=0;) {
                /* reverse output */
                dir=u8bidi_get_visual_run(pBiDi, run, &logicalStart, &runLength);
                src=text+logicalStart;

                if(UBIDI_LTR==dir) {
                    if(/*run<runCount-1 &&*/ dirProps[logicalStart+runLength-1]!=L) {
                        if(destSize>0) {
							int32_t idx{};
							U8_APPEND_UNSAFE(dest, idx, LRM_CHAR);
							dest += idx;
                        }
                        --destSize;
                    }

                    runLength=doWriteReverse(src, runLength,
                                             dest, destSize,
                                             (uint16_t)(options&~UBIDI_DO_MIRRORING), pErrorCode);
                    if(dest!=nullptr) {
                      dest+=runLength;
                    }
                    destSize-=runLength;

                    if(/*run>0 &&*/ dirProps[logicalStart]!=L) {
                        if(destSize>0) {
							int32_t idx{};
							U8_APPEND_UNSAFE(dest, idx, LRM_CHAR);
							dest += idx;
                        }
                        --destSize;
                    }
                } else {
                    if(/*run<runCount-1 &&*/ !(MASK_R_AL&DIRPROP_FLAG(dirProps[logicalStart]))) {
                        if(destSize>0) {
							int32_t idx{};
							U8_APPEND_UNSAFE(dest, idx, RLM_CHAR);
							dest += idx;
                        }
                        --destSize;
                    }

                    runLength=doWriteForward(src, runLength,
                                             dest, destSize,
                                             options, pErrorCode);
                    if(dest!=nullptr) {
                      dest+=runLength;
                    }
                    destSize-=runLength;

                    if(/*run>0 &&*/ !(MASK_R_AL&DIRPROP_FLAG(dirProps[logicalStart+runLength-1]))) {
                        if(destSize>0) {
							int32_t idx{};
							U8_APPEND_UNSAFE(dest, idx, RLM_CHAR);
							dest += idx;
                        }
                        --destSize;
                    }
                }
            }
        }
    }

    return u_terminateChars(saveDest, destCapacity, destCapacity-destSize, pErrorCode);
}

// Static Functions

static int32_t doWriteForward(const char *src, int32_t srcLength, char *dest, int32_t destSize, uint16_t options,
		UErrorCode *pErrorCode) {
    /* optimize for several combinations of options */
    switch(options&(UBIDI_REMOVE_BIDI_CONTROLS|UBIDI_DO_MIRRORING)) {
    case 0: {
        /* simply copy the LTR run to the destination */
        int32_t length=srcLength;
        if(destSize<length) {
            *pErrorCode=U_BUFFER_OVERFLOW_ERROR;
            return srcLength;
        }
        do {
            *dest++=*src++;
        } while(--length>0);
        return srcLength;
    }
    case UBIDI_DO_MIRRORING: {
        /* do mirroring */
        int32_t i=0, j=0;
        UChar32 c;

        if(destSize<srcLength) {
            *pErrorCode=U_BUFFER_OVERFLOW_ERROR;
            return srcLength;
        }
        do {
            U8_NEXT(src, i, srcLength, c);
            c=u_charMirror(c);
            U8_APPEND_UNSAFE(dest, j, c);
        } while(i<srcLength);
        return srcLength;
    }
    case UBIDI_REMOVE_BIDI_CONTROLS: {
        /* copy the LTR run and remove any BiDi control characters */
        int32_t remaining=destSize;
        char16_t c;
        do {
            c=*src++;
            if(!IS_BIDI_CONTROL_CHAR(c)) {
                if(--remaining<0) {
                    *pErrorCode=U_BUFFER_OVERFLOW_ERROR;

                    /* preflight the length */
                    while(--srcLength>0) {
                        c=*src++;
                        if(!IS_BIDI_CONTROL_CHAR(c)) {
                            --remaining;
                        }
                    }
                    return destSize-remaining;
                }
                *dest++=c;
            }
        } while(--srcLength>0);
        return destSize-remaining;
    }
    default: {
        /* remove BiDi control characters and do mirroring */
        int32_t remaining=destSize;
        int32_t i, j=0;
        UChar32 c;
        do {
            i=0;
            U8_NEXT(src, i, srcLength, c);
            src+=i;
            srcLength-=i;
            if(!IS_BIDI_CONTROL_CHAR(c)) {
                remaining-=i;
                if(remaining<0) {
                    *pErrorCode=U_BUFFER_OVERFLOW_ERROR;

                    /* preflight the length */
                    while(srcLength>0) {
                        c=*src++;
                        if(!IS_BIDI_CONTROL_CHAR(c)) {
                            --remaining;
                        }
                        --srcLength;
                    }
                    return destSize-remaining;
                }
                c=u_charMirror(c);
                U8_APPEND_UNSAFE(dest, j, c);
            }
        } while(srcLength>0);
        return j;
    }
    } /* end of switch */
}

static int32_t doWriteReverse(const char *src, int32_t srcLength, char *dest, int32_t destSize,
        uint16_t options, UErrorCode *pErrorCode) {
    /*
     * RTL run -
     *
     * RTL runs need to be copied to the destination in reverse order
     * of code points, not code units, to keep Unicode characters intact.
     *
     * The general strategy for this is to read the source text
     * in backward order, collect all code units for a code point
     * (and optionally following combining characters, see below),
     * and copy all these code units in ascending order
     * to the destination for this run.
     *
     * Several options request whether combining characters
     * should be kept after their base characters,
     * whether BiDi control characters should be removed, and
     * whether characters should be replaced by their mirror-image
     * equivalent Unicode characters.
     */
    int32_t i, j;
    UChar32 c;

    /* optimize for several combinations of options */
    switch(options&(UBIDI_REMOVE_BIDI_CONTROLS|UBIDI_DO_MIRRORING|UBIDI_KEEP_BASE_COMBINING)) {
    case 0:
        /*
         * With none of the "complicated" options set, the destination
         * run will have the same length as the source run,
         * and there is no mirroring and no keeping combining characters
         * with their base characters.
         */
        if(destSize<srcLength) {
            *pErrorCode=U_BUFFER_OVERFLOW_ERROR;
            return srcLength;
        }
        destSize=srcLength;

        /* preserve character integrity */
        do {
            /* i is always after the last code unit known to need to be kept in this segment */
            i=srcLength;

            /* collect code units for one base character */
            U8_BACK_1((const uint8_t*)src, 0, srcLength);

            /* copy this base character */
            j=srcLength;
            do {
                *dest++=src[j++];
            } while(j<i);
        } while(srcLength>0);
        break;
    case UBIDI_KEEP_BASE_COMBINING:
        /*
         * Here, too, the destination
         * run will have the same length as the source run,
         * and there is no mirroring.
         * We do need to keep combining characters with their base characters.
         */
        if(destSize<srcLength) {
            *pErrorCode=U_BUFFER_OVERFLOW_ERROR;
            return srcLength;
        }
        destSize=srcLength;

        /* preserve character integrity */
        do {
            /* i is always after the last code unit known to need to be kept in this segment */
            i=srcLength;

            /* collect code units and modifier letters for one base character */
            do {
                U8_PREV(src, 0, srcLength, c);
            } while(srcLength>0 && IS_COMBINING(u_charType(c)));

            /* copy this "user character" */
            j=srcLength;
            do {
                *dest++=src[j++];
            } while(j<i);
        } while(srcLength>0);
        break;
    default:
        /*
         * With several "complicated" options set, this is the most
         * general and the slowest copying of an RTL run.
         * We will do mirroring, remove BiDi controls, and
         * keep combining characters with their base characters
         * as requested.
         */
        if(!(options&UBIDI_REMOVE_BIDI_CONTROLS)) {
            i=srcLength;
        } else {
            /* we need to find out the destination length of the run,
               which will not include the BiDi control characters */
            int32_t length=srcLength;
            char16_t ch;

            i=0;
            do {
                ch=*src++;
                if(!IS_BIDI_CONTROL_CHAR(ch)) {
                    ++i;
                }
            } while(--length>0);
            src-=srcLength;
        }

        if(destSize<i) {
            *pErrorCode=U_BUFFER_OVERFLOW_ERROR;
            return i;
        }
        destSize=i;

        /* preserve character integrity */
        do {
            /* i is always after the last code unit known to need to be kept in this segment */
            i=srcLength;

            /* collect code units for one base character */
            U8_PREV(src, 0, srcLength, c);
            if(options&UBIDI_KEEP_BASE_COMBINING) {
                /* collect modifier letters for this base character */
                while(srcLength>0 && IS_COMBINING(u_charType(c))) {
                    U8_PREV(src, 0, srcLength, c);
                }
            }

            if(options&UBIDI_REMOVE_BIDI_CONTROLS && IS_BIDI_CONTROL_CHAR(c)) {
                /* do not copy this BiDi control character */
                continue;
            }

            /* copy this "user character" */
            j=srcLength;
            if(options&UBIDI_DO_MIRRORING) {
                /* mirror only the base character */
                int32_t k=0;
                c=u_charMirror(c);
                U8_APPEND_UNSAFE(dest, k, c);
                dest+=k;
                j+=k;
            }
            while(j<i) {
                *dest++=src[j++];
            }
        } while(srcLength>0);
        break;
    } /* end of switch */

    return destSize;
}

