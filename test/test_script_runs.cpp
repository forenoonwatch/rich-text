#include <catch2/catch_test_macros.hpp>

#include <usc_impl.h>
#include <unicode/ustring.h>

#include <array>

namespace {

struct RunTestData {
	const char* runText;
	UScriptCode runCode;
};

}

static constexpr const RunTestData g_scriptRunTestData1[] = {
	{"\\u0020\\u0946\\u0939\\u093F\\u0928\\u094D\\u0926\\u0940\\u0020", USCRIPT_DEVANAGARI},
	{"\\u0627\\u0644\\u0639\\u0631\\u0628\\u064A\\u0629\\u0020", USCRIPT_ARABIC},
	{"\\u0420\\u0443\\u0441\\u0441\\u043A\\u0438\\u0439\\u0020", USCRIPT_CYRILLIC},
	{"English (", USCRIPT_LATIN},
	{"\\u0E44\\u0E17\\u0E22", USCRIPT_THAI},
	{") ", USCRIPT_LATIN},
	{"\\u6F22\\u5B75", USCRIPT_HAN},
	{"\\u3068\\u3072\\u3089\\u304C\\u306A\\u3068", USCRIPT_HIRAGANA},
	{"\\u30AB\\u30BF\\u30AB\\u30CA", USCRIPT_KATAKANA},
	{"\\U00010400\\U00010401\\U00010402\\U00010403", USCRIPT_DESERET},
};

static constexpr const RunTestData g_scriptRunTestData2[] = {
	{"((((((((((abc))))))))))", USCRIPT_LATIN},
};

static void test_script_runs_icu(const RunTestData* pTestData, size_t testCount);

TEST_CASE("ICU Script Runs", "[ScriptRuns]") {
	test_script_runs_icu(g_scriptRunTestData1, std::ssize(g_scriptRunTestData1));
	test_script_runs_icu(g_scriptRunTestData2, std::ssize(g_scriptRunTestData2));
}

static void test_script_runs_icu(const RunTestData* pTestData, size_t testCount) {
	UChar testString[1024];
	int32_t runStarts[256];

	// Fill in the test string and the runStarts array.
	int32_t stringLimit = 0;
	for (size_t run = 0; run < testCount; ++run) {
		runStarts[run] = stringLimit;
		stringLimit += u_unescape(pTestData[run].runText, testString + stringLimit, 1024 - stringLimit);
	}

	// The limit of the last run
	runStarts[testCount] = stringLimit;

	UErrorCode err{U_ZERO_ERROR};
	UScriptRun* pScriptRun = uscript_openRun(testString, stringLimit, &err);
	int32_t runStart, runLimit;
	UScriptCode runCode;
	size_t runIndex{};

	while (uscript_nextRun(pScriptRun, &runStart, &runLimit, &runCode)) {
		REQUIRE(runStart == runStarts[runIndex]);
		REQUIRE(runLimit == runStarts[runIndex + 1]);
		REQUIRE(runCode == pTestData[runIndex].runCode);
		REQUIRE(runIndex < testCount);

		++runIndex;
	}

	uscript_closeRun(pScriptRun);
}

