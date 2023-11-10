#include <benchmark/benchmark.h>

#include "bidi_test.hpp"

#include <vector>
#include <memory>
#include <string_view>
#include <cstring>

#include <unicode/ustring.h>

extern "C" {
#include <SheenBidi.h>
}
//static constexpr const size_t TEST_STRING_SIZE = 16 * 1024 * 1024; 
static constexpr const size_t TEST_STRING_SIZE = 1 * 1024 * 1024; 
static std::unique_ptr<char[]> g_testString;
static std::unique_ptr<char16_t[]> g_testString16;
static std::vector<std::string_view> g_testViews;
static std::vector<std::u16string_view> g_testViews16;
static size_t g_testStringLength{};
static size_t g_baseViewCount{};

static void init_test_string();

static size_t bench_sheen(std::u16string_view str);
static size_t bench_icu(std::u16string_view str);

static void BM_Sheen(benchmark::State& state) {
	init_test_string();

	for (auto _ : state) {
		size_t checkSum{};

		for (auto& str : g_testViews16) {
			checkSum += bench_sheen(str);
		}

		benchmark::DoNotOptimize(checkSum);
	}
}

static void BM_ICU(benchmark::State& state) {
	init_test_string();

	for (auto _ : state) {
		size_t checkSum{};

		for (auto& str : g_testViews16) {
			checkSum += bench_icu(str);
		}

		benchmark::DoNotOptimize(checkSum);
	}
}

BENCHMARK(BM_Sheen);
BENCHMARK(BM_ICU);

BENCHMARK_MAIN();

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

	for (int i = 0; i < testStringCount; ++i) {
		auto len = getStringFromDirProps(testStrings[i].text, testStrings[i].length, stringIter);
		g_testViews.emplace_back(stringIter, len);
		stringIter += len;
	}

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

	/*auto* stringIter16 = g_testString16.get();

	for (auto& str8 : g_testViews) {
		int32_t len;
		u_strFromUTF8(stringIter16, TEST_STRING_SIZE - (stringIter16 - g_testString16.get()), &len,
				str8.data(), str8.size(), &err);
		g_testViews16.emplace_back(stringIter16, len);
		stringIter16 += len;
	}*/

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

