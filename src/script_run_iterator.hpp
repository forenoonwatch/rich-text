// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
**********************************************************************
*   Copyright (C) 1999-2011, International Business Machines
*   Corporation and others.  All Rights Reserved.
**********************************************************************
*
* File USC_IMPL.H
*
* Modification History:
*
*   Date        Name        Description
*   10/29/2023  Anon 		Converted to UTF-8 ScriptRunIterator
*   07/08/2002  Eric Mader  Creation.
******************************************************************************
*/

#pragma once

#include <unicode/uscript.h>

class ScriptRunIterator final {
	public:
		explicit ScriptRunIterator(const char* text, int32_t textLength);

		/**
		 * Advances to the next script run, returning the start and limit offsets, and the script of the run.
		 *
		 * @return true if there was another run
		 */
		bool next(int32_t& outRunStart, int32_t& outRunLimit, UScriptCode& outRunScript);
	private:
		static constexpr const size_t PAREN_STACK_DEPTH = 32;

		struct ParenStackEntry {
			int32_t pairIndex;
			UScriptCode scriptCode;
		};

		const char* m_text;
		int32_t m_textLength;
		int32_t m_scriptLimit;
		ParenStackEntry m_parenStack[PAREN_STACK_DEPTH];
		int32_t m_parenSP;
		int32_t m_pushCount;
		int32_t m_fixupCount;

		void push(int32_t pairIndex, UScriptCode scriptCode);
		void pop();
		void fixup(UScriptCode scriptCode);
};

