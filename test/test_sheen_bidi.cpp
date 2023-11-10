#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

#include "bidi_test.hpp"

#include <unicode/ustring.h>
#include <unicode/ucnv.h>
#include <unicode/ushape.h>
#include <unicode/utf8.h>
#include <unicode/utf16.h>
#include <cstring.h>

extern "C" {
#include <SheenBidi.h>
}

namespace {

struct LogicalRun {
	size_t start;
	size_t limit;
	SBLevel level;
};

struct ParagraphRun {
	size_t start;
	size_t limit;
	SBLevel level;
};

}

static constexpr const size_t TEST_STRING_SIZE = 16 * 1024; 
static std::unique_ptr<char[]> g_testString;
static std::unique_ptr<char16_t[]> g_testString16;
static std::vector<std::string_view> g_testViews;
static std::vector<std::u16string_view> g_testViews16;
static size_t g_testStringLength{};
static size_t g_baseViewCount{};

static void compare_icu_sheen(std::string_view str);
static void print_levels(const char* name, const uint8_t* levels, size_t count);
static void print_visual_runs_icu(UBiDi* pBiDi, int32_t runCount);
static void print_visual_runs_sheen(const SBRun* runs, size_t runCount);

static void init_test_string();

static size_t bench_sheen(std::u16string_view str);
static size_t bench_icu(std::u16string_view str);

TEST_CASE("TestBiDi", "[SheenBidiPerf]") {
	init_test_string();

	BENCHMARK_ADVANCED("Sheen")(Catch::Benchmark::Chronometer meter) {
		meter.measure([] {
			size_t checkSum{};

			for (auto& view : g_testViews16) {
				checkSum += bench_sheen(view);
			}

			return checkSum;
		});
	};
	
	BENCHMARK_ADVANCED("ICU")(Catch::Benchmark::Chronometer meter) {
		meter.measure([] {
			size_t checkSum{};

			for (auto& view : g_testViews16) {
				checkSum += bench_icu(view);
			}

			return checkSum;
		});
	};

	char str[16]{};
	std::strcat(str, "Hello");

	BENCHMARK(str) {};
}

TEST_CASE("ComparisonTest", "[SheenBidi]") {
	init_test_string();

	for (size_t i = 0; i < g_baseViewCount; ++i) {
		printf("TEST STRING %llu =======================================================================\n", i);
		compare_icu_sheen(g_testViews[i]);
	}
}

// Static Functions

static void compare_icu_sheen(std::string_view str) {
	if (str.empty()) {
		return;
	}

	char16_t* u16Str = (char16_t*)std::malloc(str.size() * sizeof(char16_t));
	int32_t u16Length;
	UErrorCode err{};
	u_strFromUTF8(u16Str, str.size(), &u16Length, str.data(), str.size(), &err);

	auto* pParaBiDi = ubidi_open();
	auto* pLineBiDi = ubidi_open();
	auto* pSubParaBiDi = ubidi_open();

	SBCodepointSequence codepointSequence{SBStringEncodingUTF16, (void*)u16Str, (size_t)u16Length};
	SBAlgorithmRef bidiAlgorithm = SBAlgorithmCreate(&codepointSequence);
	ubidi_setPara(pParaBiDi, u16Str, u16Length, UBIDI_DEFAULT_LTR, nullptr, &err);

	SBUInteger paragraphOffset{};
	std::vector<ParagraphRun> sheenParagraphs;

	while (paragraphOffset < u16Length) {
		SBUInteger actualLength, separatorLength;
		SBAlgorithmGetParagraphBoundary(bidiAlgorithm, paragraphOffset, INT32_MAX, &actualLength,
				&separatorLength);
		SBParagraphRef paragraph = SBAlgorithmCreateParagraph(bidiAlgorithm, paragraphOffset, actualLength,
				SBLevelDefaultLTR);
		auto paraLevel = SBParagraphGetBaseLevel(paragraph);
		sheenParagraphs.emplace_back(paragraphOffset, paragraphOffset + actualLength, paraLevel);
		paragraphOffset += actualLength;
		SBParagraphRelease(paragraph);
	}

	auto icuParaCount = ubidi_countParagraphs(pParaBiDi);
	REQUIRE(sheenParagraphs.size() == icuParaCount);

	for (int32_t i = 0; i < icuParaCount; ++i) {
		int32_t icuParaStart, icuParaLimit;
		UBiDiLevel icuParaLevel;
		ubidi_getParagraphByIndex(pParaBiDi, i, &icuParaStart, &icuParaLimit, &icuParaLevel, &err);
		REQUIRE(U_SUCCESS(err));

		REQUIRE(icuParaStart == sheenParagraphs[i].start);
		REQUIRE(icuParaLimit == sheenParagraphs[i].limit);
		REQUIRE(icuParaLevel == sheenParagraphs[i].level);
	}

	auto* icuParaLevels = ubidi_getLevels(pParaBiDi, &err);

	for (int32_t i = 0; i < icuParaCount; ++i) {
		auto paragraphLength = sheenParagraphs[i].limit - sheenParagraphs[i].start;
		SBParagraphRef paragraph = SBAlgorithmCreateParagraph(bidiAlgorithm, sheenParagraphs[i].start,
				sheenParagraphs[i].limit - sheenParagraphs[i].start, SBLevelDefaultLTR);
		const SBLevel* sheenLevels = SBParagraphGetLevelsPtr(paragraph);

		print_levels("Sheen", sheenLevels, paragraphLength);
		print_levels("ICU", icuParaLevels + sheenParagraphs[i].start, paragraphLength);

		//std::memcpy((SBLevel*)sheenLevels, icuParaLevels + sheenParagraphs[i].start,
				//paragraphLength * sizeof(SBLevel));

		SBLineRef paragraphLine = SBParagraphCreateLine(paragraph, sheenParagraphs[i].start, paragraphLength);
		SBUInteger sheenVisualRunCount = SBLineGetRunCount(paragraphLine);
		const SBRun* sheenRuns = SBLineGetRunsPtr(paragraphLine);

		std::vector<LogicalRun> sheenLogicalRuns;
		SBLevel sheenLevel = sheenLevels[0];
		size_t lastSheenLevelStart = 0;
		size_t sheenLevelCount = 0;

		for (size_t i = 1; i < paragraphLength; ++i) {
			if (sheenLevels[i] != sheenLevel) {
				sheenLogicalRuns.emplace_back(lastSheenLevelStart, i, sheenLevel);
				sheenLevel = sheenLevels[i];
				lastSheenLevelStart = i;
				++sheenLevelCount;
			}
		}

		sheenLogicalRuns.emplace_back(lastSheenLevelStart, paragraphLength, sheenLevel);

		/*ubidi_setPara(pSubParaBiDi, u16Str + sheenParagraphs[i].start, paragraphLength, UBIDI_DEFAULT_LTR,
				nullptr, &err);
		REQUIRE(U_SUCCESS(err));
		auto icuLogicalRunCount = ubidi_countRuns(pSubParaBiDi, &err);*/
		//CHECK(icuLogicalRunCount == sheenLogicalRuns.size());

		ubidi_setLine(pParaBiDi, sheenParagraphs[i].start, sheenParagraphs[i].limit, pLineBiDi, &err);
		auto icuVisualRunCount = ubidi_countRuns(pLineBiDi, &err);
		REQUIRE(U_SUCCESS(err));

		//CHECK(icuVisualRunCount == sheenVisualRunCount);
		bool allEqual = true;

		if (icuVisualRunCount == sheenVisualRunCount) {
			for (int32_t i = 0; i < icuVisualRunCount; ++i) {
				int32_t logicalStart, runLength;
				auto dir = ubidi_getVisualRun(pLineBiDi, i, &logicalStart, &runLength);
				auto& run = sheenRuns[i];
				allEqual = allEqual && (run.offset == logicalStart);
				allEqual = allEqual && (run.length == runLength);
				allEqual = allEqual && ((run.level & 1) == dir);
			}
		}
		else {
			allEqual = false;
		}

		CHECK(allEqual);

		print_visual_runs_icu(pLineBiDi, icuVisualRunCount);
		print_visual_runs_sheen(sheenRuns, sheenVisualRunCount);

		SBLineRelease(paragraphLine);
		SBParagraphRelease(paragraph);
	}

	ubidi_close(pLineBiDi);
	ubidi_close(pParaBiDi);
	ubidi_close(pSubParaBiDi);

	std::free(u16Str);

	SBAlgorithmRelease(bidiAlgorithm);
}

static void print_levels(const char* name, const uint8_t* levels, size_t count) {
	printf("%s:\t\t [", name);

	if (!levels) {
		puts("NULL]");
		return;
	}

	char buffer[16]{};

	for (size_t i = 0; i < count; ++i) {
		auto cnt = snprintf(buffer, 16, "%hhu", levels[i]);

		for (int j = 0; j < 2 - cnt; ++j) {
			putchar(' ');
		}

		printf("%s", buffer);

		if (i != count - 1) {
			printf(", ");
		}
		else {
			printf("]\n");
		}
	}
}

static void print_visual_runs_icu(UBiDi* pBiDi, int32_t runCount) {
	printf("[VisualRuns] ICU:\t[");

	for (int32_t i = 0; i < runCount; ++i) {
		int32_t logicalStart, runLength;
		auto dir = ubidi_getVisualRun(pBiDi, i, &logicalStart, &runLength);

		if (i != runCount - 1) {
			printf("{%d-%d, %s}, ", logicalStart, logicalStart + runLength, dir == UBIDI_LTR ? "LTR" : "RTL");
		}
		else {
			printf("{%d-%d, %s}]\n", logicalStart, logicalStart + runLength, dir == UBIDI_LTR ? "LTR" : "RTL");
		}
	}
}

static void print_visual_runs_sheen(const SBRun* runs, size_t runCount) {
	printf("[VisualRuns] Sheen:\t[");

	for (size_t i = 0; i < runCount; ++i) {
		auto& run = runs[i];

		if (i != runCount - 1) {
			printf("{%llu-%llu, %s}, ", run.offset, run.offset + run.length,
					(run.level & 1) == 0 ? "LTR" : "RTL");
		}
		else {
			printf("{%llu-%llu, %s}]\n", run.offset, run.offset + run.length,
					(run.level & 1) == 0 ? "LTR" : "RTL");
		}
	}
}

static void initCharFromDirProps() {
    static constexpr const UVersionInfo ucd401={ 4, 0, 1, 0 };
    static UVersionInfo ucdVersion={ 0, 0, 0, 0 };

    /* lazy initialization */
    if (ucdVersion[0] > 0) {
        return;
    }

    u_getUnicodeVersion(ucdVersion);
    if (std::memcmp(ucdVersion, ucd401, sizeof(UVersionInfo)) >= 0) {
        /* Unicode 4.0.1 changes bidi classes for +-/ */
        charFromDirProp[U_EUROPEAN_NUMBER_SEPARATOR]=0x2b; /* change ES character from / to + */
    }
}

/* return a string with characters according to the desired directional properties */
static size_t getStringFromDirProps(const uint8_t *dirProps, int32_t length, char* buffer) {
    initCharFromDirProps();

	size_t outLength{};

    for (int32_t i = 0; i < length; ++i) {
		U8_APPEND_UNSAFE(buffer, outLength, charFromDirProp[dirProps[i]]);
    }

	return outLength;
}

static void init_test_string() {
	if (g_testString) {
		return;
	}

	g_testString = std::make_unique_for_overwrite<char[]>(TEST_STRING_SIZE);
	char* stringIter = g_testString.get();

	FILE* file = fopen("test_cases.txt", "wb");

	for (int i = 0; i < testStringCount; ++i) {
		auto len = getStringFromDirProps(testStrings[i].text, testStrings[i].length, stringIter);
		fwrite(stringIter, 1, len, file);
		fputc('\n', file);
		g_testViews.emplace_back(stringIter, len);
		stringIter += len;
	}

	fclose(file);

	g_testStringLength = stringIter - g_testString.get();
	g_baseViewCount = g_testViews.size();

	for (;;) {
		auto nextString = g_testViews[rand() % g_baseViewCount];

		if ((stringIter - g_testString.get()) + nextString.size() >= TEST_STRING_SIZE) {
			break;
		}

		g_testViews.emplace_back(stringIter, nextString.size());

		std::memcpy(stringIter, nextString.data(), nextString.size());
		stringIter += nextString.size();
	}

	g_testString16 = std::make_unique_for_overwrite<char16_t[]>(TEST_STRING_SIZE);
	UErrorCode err{};
	int32_t stringSize16;
	u_strFromUTF8(g_testString16.get(), TEST_STRING_SIZE, &stringSize16, g_testString.get(),
			stringIter - g_testString.get(), &err);

	constexpr const size_t BUCKET_STEP = 2;
	size_t testBucketSize = 512;
	size_t lastBucketSize = testBucketSize / BUCKET_STEP;

	while (testBucketSize < stringSize16) {
		auto sizeRange = (testBucketSize - lastBucketSize) / 2;
		auto minStringSize = lastBucketSize + sizeRange;
		const auto* it = g_testString16.get();
		const auto* end = it + stringSize16;

		for (size_t viewCount = 0; viewCount < 20; ++viewCount) {
			auto nextStringSize = rand() % sizeRange + minStringSize;

			if (it + nextStringSize > end) {
				break;
			}

			g_testViews16.emplace_back(it, nextStringSize);
			it += nextStringSize;
		}

		lastBucketSize = testBucketSize;
		testBucketSize *= BUCKET_STEP;
	}

	puts("Finished building string");
}

static size_t bench_sheen(std::u16string_view str) {
	size_t checkSum{};

	SBCodepointSequence codepointSequence{SBStringEncodingUTF16, (void*)str.data(), str.size()};
	SBAlgorithmRef bidiAlgorithm = SBAlgorithmCreate(&codepointSequence);

	SBUInteger paragraphOffset{};

	while (paragraphOffset < str.size()) {
		SBUInteger actualLength, separatorLength;
		SBAlgorithmGetParagraphBoundary(bidiAlgorithm, paragraphOffset, INT32_MAX, &actualLength,
				&separatorLength);
		SBParagraphRef paragraph = SBAlgorithmCreateParagraph(bidiAlgorithm, paragraphOffset, actualLength,
				SBLevelDefaultLTR);
		SBLineRef paragraphLine = SBParagraphCreateLine(paragraph, paragraphOffset, actualLength);
		SBUInteger runCount = SBLineGetRunCount(paragraphLine);
		const SBRun *runArray = SBLineGetRunsPtr(paragraphLine);
		const SBLevel* sheenLevels = SBParagraphGetLevelsPtr(paragraph);

		// Logical Runs
		SBLevel sheenLevel = sheenLevels[0];
		size_t lastSheenLevelStart = 0;
		size_t sheenLevelCount = 0;

		for (size_t i = 1; i < actualLength; ++i) {
			if (sheenLevels[i] != sheenLevel) {
				checkSum += lastSheenLevelStart + (sheenLevel & 1) + sheenLevelCount;
				sheenLevel = sheenLevels[i];
				lastSheenLevelStart = i;
				++sheenLevelCount;
			}
		}

		checkSum += lastSheenLevelStart + (sheenLevel & 1) + sheenLevelCount;

		// Visual Runs
		for (size_t i = 0; i < runCount; ++i) {
			checkSum += runArray[i].level + runArray[i].level + runArray[i].offset;
		}

		checkSum += paragraphOffset + actualLength;

		paragraphOffset += actualLength;
		SBLineRelease(paragraphLine);
		SBParagraphRelease(paragraph);
	}

	SBAlgorithmRelease(bidiAlgorithm);

	return checkSum;
}

static size_t bench_icu(std::u16string_view str) {
	size_t checkSum{};

	auto* pParaBiDi = ubidi_open();
	auto* pLineBiDi = ubidi_open();

	UErrorCode err{};
	ubidi_setPara(pParaBiDi, str.data(), str.size(), UBIDI_DEFAULT_LTR, nullptr, &err);
	auto paragraphCount = ubidi_countParagraphs(pParaBiDi);

	for (int32_t i = 0; i < paragraphCount; ++i) {
		int32_t icuParaStart, icuParaLimit;
		UBiDiLevel icuParaLevel;
		ubidi_getParagraphByIndex(pParaBiDi, i, &icuParaStart, &icuParaLimit, &icuParaLevel, &err);

		// Visual Runs
		ubidi_setLine(pParaBiDi, icuParaStart, icuParaLimit, pLineBiDi, &err);
		auto visualRunCount = ubidi_countRuns(pLineBiDi, &err);

		for (int32_t j = 0; j < visualRunCount; ++j) {
			int32_t logicalStart, runLength;
			auto runDir = ubidi_getVisualRun(pLineBiDi, j, &logicalStart, &runLength);
			checkSum += runDir + logicalStart + runLength;
		}
	}

	// Logical Runs
	auto logicalRunCount = ubidi_countRuns(pParaBiDi, &err);
	int32_t logicalStart{};

	for (int32_t i = 0; i < logicalRunCount; ++i) {
		int32_t limit;
		UBiDiLevel level;
		ubidi_getLogicalRun(pParaBiDi, logicalStart, &limit, &level);
		checkSum += logicalStart + limit + level;
		logicalStart = limit;
	}	

	ubidi_close(pLineBiDi);
	ubidi_close(pParaBiDi);

	return checkSum;
}

