// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
**********************************************************************
*   Copyright (C) 1999-2016, International Business Machines
*   Corporation and others.  All Rights Reserved.
**********************************************************************
*
* File USC_IMPL.C
*
* Modification History:
*
*   Date		Name		Description
*   10/29/2023  Anon 		Converted to UTF-8 ScriptRunIterator
*   07/08/2002  Eric Mader  Creation.
******************************************************************************
*/
#include "script_run_iterator.hpp"

#include <unicode/utf8.h>

#define MOD(sp) ((sp) % PAREN_STACK_DEPTH)
#define LIMIT_INC(sp) (((sp) < PAREN_STACK_DEPTH)? (sp) + 1 : PAREN_STACK_DEPTH)
#define INC(sp,count) (MOD((sp) + (count)))
#define INC1(sp) (INC(sp, 1))
#define DEC(sp,count) (MOD((sp) + PAREN_STACK_DEPTH - (count)))
#define DEC1(sp) (DEC(sp, 1))
#define STACK_IS_EMPTY(scriptRun) ((scriptRun)->pushCount <= 0)
#define STACK_IS_NOT_EMPTY(scriptRun) (! STACK_IS_EMPTY(scriptRun))
#define TOP(scriptRun) ((scriptRun)->parenStack[(scriptRun)->parenSP])

static constexpr const UChar32 pairedChars[] = {
	0x0028, 0x0029, /* ascii paired punctuation */
	0x003c, 0x003e,
	0x005b, 0x005d,
	0x007b, 0x007d,
	0x00ab, 0x00bb, /* guillemets */
	0x2018, 0x2019, /* general punctuation */
	0x201c, 0x201d,
	0x2039, 0x203a,
	0x3008, 0x3009, /* chinese paired punctuation */
	0x300a, 0x300b,
	0x300c, 0x300d,
	0x300e, 0x300f,
	0x3010, 0x3011,
	0x3014, 0x3015,
	0x3016, 0x3017,
	0x3018, 0x3019,
	0x301a, 0x301b
};

static int8_t high_bit(int32_t value);
static int32_t get_pair_index(UChar32 ch);
static UBool script_is_same(UScriptCode scriptOne, UScriptCode scriptTwo);

ScriptRunIterator::ScriptRunIterator(const char* text, int32_t textLength)
		: m_text{text}
		, m_textLength{textLength}
		, m_scriptLimit{}
		, m_parenSP{-1}
		, m_pushCount{}
		, m_fixupCount{} {}

bool ScriptRunIterator::next(int32_t& outRunStart, int32_t& outRunLimit, UScriptCode& outRunScript) {
	/* if we've fallen off the end of the text, we're done */
	if (m_scriptLimit >= m_textLength) {
		return false;
	}

	m_fixupCount = 0;

	auto scriptStart = m_scriptLimit;
	UScriptCode scriptCode = USCRIPT_COMMON;
	UChar32 ch;
	UErrorCode err{};

	for (; m_scriptLimit < m_textLength;) {
		U8_GET((const uint8_t*)m_text, 0, m_scriptLimit, m_textLength, ch);
		auto sc = uscript_getScript(ch, &err);
		auto pairIndex = get_pair_index(ch);

		/*
		 * Paired character handling:
		 *
		 * if it's an open character, push it onto the stack.
		 * if it's a close character, find the matching open on the
		 * stack, and use that script code. Any non-matching open
		 * characters above it on the stack will be poped.
		 */
		if (pairIndex >= 0) {
			if ((pairIndex & 1) == 0) {
				push(pairIndex, scriptCode);
			}
			else {
				int32_t pi = pairIndex & ~1;

				while (m_pushCount > 0 && m_parenStack[m_parenSP].pairIndex != pi) {
					pop();
				}

				if (m_pushCount > 0) {
					sc = m_parenStack[m_parenSP].scriptCode;
				}
			}
		}

		if (script_is_same(scriptCode, sc)) {
			if (scriptCode <= USCRIPT_INHERITED && sc > USCRIPT_INHERITED) {
				scriptCode = sc;
				fixup(scriptCode);
			}

			/*
			 * if this character is a close paired character,
			 * pop the matching open character from the stack
			 */
			if (pairIndex >= 0 && (pairIndex & 1) != 0) {
				pop();
			}
		}
		else {
			break;
		}

		U8_FWD_1(m_text, m_scriptLimit, m_textLength);
	}

	outRunStart = scriptStart;
	outRunLimit = m_scriptLimit;
	outRunScript = scriptCode;

	return true;
}

void ScriptRunIterator::push(int32_t pairIndex, UScriptCode scriptCode) {
	m_pushCount  = LIMIT_INC(m_pushCount);
	m_fixupCount = LIMIT_INC(m_fixupCount);
	
	m_parenSP = INC1(m_parenSP);
	m_parenStack[m_parenSP].pairIndex  = pairIndex;
	m_parenStack[m_parenSP].scriptCode = scriptCode;
}

void ScriptRunIterator::pop() {
	if (m_pushCount <= 0) {
		return;
	}
	
	if (m_fixupCount > 0) {
		m_fixupCount -= 1;
	}
	
	m_pushCount -= 1;
	m_parenSP = DEC1(m_parenSP);
	
	/* If the stack is now empty, reset the stack
	   pointers to their initial values.
	 */
	if (m_pushCount <= 0) {
		m_parenSP = -1;
	}
}

void ScriptRunIterator::fixup(UScriptCode scriptCode) {
	int32_t fixupSP = DEC(m_parenSP, m_fixupCount);
	
	while (m_fixupCount-- > 0) {
		fixupSP = INC1(fixupSP);
		m_parenStack[fixupSP].scriptCode = scriptCode;
	}
}

static int8_t high_bit(int32_t value) {
	int8_t bit = 0;

	if (value <= 0) {
		return -32;
	}

	if (value >= 1 << 16) {
		value >>= 16;
		bit += 16;
	}

	if (value >= 1 << 8) {
		value >>= 8;
		bit += 8;
	}

	if (value >= 1 << 4) {
		value >>= 4;
		bit += 4;
	}

	if (value >= 1 << 2) {
		value >>= 2;
		bit += 2;
	}

	if (value >= 1 << 1) {
		//value >>= 1;
		bit += 1;
	}

	return bit;
}

static int32_t get_pair_index(UChar32 ch) {
	int32_t pairedCharCount = sizeof(pairedChars) / sizeof(UChar32);
	int32_t pairedCharPower = 1 << high_bit(pairedCharCount);
	int32_t pairedCharExtra = pairedCharCount - pairedCharPower;

	int32_t probe = pairedCharPower;
	int32_t pairIndex = 0;

	if (ch >= pairedChars[pairedCharExtra]) {
		pairIndex = pairedCharExtra;
	}

	while (probe > (1 << 0)) {
		probe >>= 1;

		if (ch >= pairedChars[pairIndex + probe]) {
			pairIndex += probe;
		}
	}

	if (pairedChars[pairIndex] != ch) {
		pairIndex = -1;
	}

	return pairIndex;
}

static UBool script_is_same(UScriptCode scriptOne, UScriptCode scriptTwo) {
	return scriptOne <= USCRIPT_INHERITED || scriptTwo <= USCRIPT_INHERITED || scriptOne == scriptTwo;
}

