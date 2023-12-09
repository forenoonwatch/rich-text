#include <benchmark/benchmark.h>

#include <font_cache.hpp>
#include <pair.hpp>
#include <layout_info.hpp>

#include <memory>
#include <random>
#include <cmath>
#include <span>
#include <cstdio>

#include <unicode/utf8.h>

static constexpr const size_t TEST_STRING_SIZE = 1 * 1024 * 1024;
static constexpr const double WORD_SIZE_AVERAGE = 15.0;
static constexpr const double WORD_SIZE_STDDEV = 5.0;
static constexpr const double PARA_SIZE_AVERAGE = 150.0;
static constexpr const double PARA_SIZE_STDDEV = 75.0;

static constexpr const Text::Pair<uint32_t, uint32_t> g_unicodeLatin[] = {
	// Latin, excluding control chars and whitespace
	{0x21u, 0x7Eu},
	// Latin-1 excluding control chars
	//{0xA0u, 0xFFu},
	// Latin Extended-A
	{0x100u, 0x17Fu},
};

static constexpr const Text::Pair<uint32_t, uint32_t> g_unicodeHebrew[] = {
	// Hebrew
	{0x591u, 0x5C7u},
	{0x5D0u, 0x5EAu},
	{0x5EFu, 0x5F4u},
};

static constexpr const Text::Pair<uint32_t, uint32_t> g_unicodeArabic[] = {
	// Arabic
	{0x600u, 0x61Bu},
	{0x61Eu, 0x6FFu},
};

static constexpr const Text::Pair<uint32_t, uint32_t> g_unicodeDevanagari[] = {
	// Devanagari
	{0x900, 0x97F},
};

static constexpr const Text::Pair<uint32_t, uint32_t> g_unicodeCJK[] = {
	// Hiragana
	{0x3041u, 0x3096u},
	{0x3099u, 0x309Fu},
	// Katakana
	{0x30A0u, 0x30FFu},
	// CJK Unified Ideographs
	{0x4E00u, 0x9FFFu},
};

static constexpr const Text::Pair<uint32_t, uint32_t> g_unicodeSymbols[] = {
	// Misc symbols and pictographs, Emoticons
	{0x1F300u, 0x1F64F},
	// Mathematical Operators
	{0x2200u, 0x22FFu},
};

static constexpr const std::span<const Text::Pair<uint32_t, uint32_t>> g_unicodeLangs[] = {
	{g_unicodeLatin},
	{g_unicodeHebrew},
	{g_unicodeArabic},
	{g_unicodeDevanagari},
	{g_unicodeCJK},
	{g_unicodeSymbols},
};

static constexpr const uint32_t g_whitespace[] = {
	0x9u, // TAB
	0x20u, // SPACE
	0xA0u, // NBSP
	0x3000u, // CJK Ideographic Space
};

static constexpr const uint32_t g_paragraphSeparators[] = {
	0xAu, // LF
	0xDu, // CR
	0x2028u, // LSEP
	0x2029u, // PSEP
};

static std::unique_ptr<char[]> g_testString;
static size_t g_testStringSize{};
static Text::ValueRuns<uint32_t> g_fontStyles;

static void init_test_string();
static void dump_test_data();

// Benchmarks

static void BM_Layout_SingleFont_LineBreak(benchmark::State& state) {
	init_test_string();
	FontCache fontCache("fonts/families");

	std::string_view str(g_testString.get(), g_testStringSize);

	auto famIdx = fontCache.get_font_family("Noto Sans"); 
	auto font = fontCache.get_font(famIdx, FontWeightIndex::REGULAR, FontFaceStyle::NORMAL, 48);
	Text::ValueRuns<const MultiScriptFont*> fontRuns(&font, str.size());

	for (auto _ : state) {
		Text::LayoutInfo layoutInfo;
		Text::build_layout_info_utf8(layoutInfo, str.data(), str.size(), fontRuns, 100.f, 100.f,
				TextYAlignment::TOP, Text::LayoutInfoFlags::NONE);
		benchmark::DoNotOptimize(layoutInfo);
	}
}

static void BM_Layout_SingleFont_NoLineBreak(benchmark::State& state) {
	init_test_string();
	FontCache fontCache("fonts/families");

	std::string_view str(g_testString.get(), g_testStringSize);

	auto famIdx = fontCache.get_font_family("Noto Sans"); 
	auto font = fontCache.get_font(famIdx, FontWeightIndex::REGULAR, FontFaceStyle::NORMAL, 48);
	Text::ValueRuns<const MultiScriptFont*> fontRuns(&font, str.size());

	for (auto _ : state) {
		Text::LayoutInfo layoutInfo;
		Text::build_layout_info_utf8(layoutInfo, str.data(), str.size(), fontRuns, 0.f, 100.f,
				TextYAlignment::TOP, Text::LayoutInfoFlags::NONE);
		benchmark::DoNotOptimize(layoutInfo);
	}
}

/*static void BM_Layout_MultiFont_LineBreak(benchmark::State& state) {
	init_test_string();
	FontCache fontCache("fonts/families");

	std::string_view str(g_testString.get(), g_testStringSize);

	auto famIdx = fontCache.get_font_family("Noto Sans"); 
	auto font = fontCache.get_font(famIdx, FontWeightIndex::REGULAR, FontFaceStyle::NORMAL, 48);
	std::vector<MultiScriptFont> fonts;
	fonts.reserve(g_fontStyles.get_run_count());
	Text::ValueRuns<const MultiScriptFont*> fontRuns;

	for (size_t i = 0; i < g_fontStyles.get_run_count(); ++i) {
		auto value = g_fontStyles.get_run_value(i);
		auto size = value >> 16;
		auto weight = static_cast<FontWeightIndex>((value >> 1) & 0xF);
		auto style = (value & 1) == 0 ? FontFaceStyle::NORMAL : FontFaceStyle::ITALIC;

		fonts.emplace_back(fontCache.get_font(famIdx, weight, style, size));
	}

	for (size_t i = 0; i < g_fontStyles.get_run_count(); ++i) {
		auto limit = g_fontStyles.get_run_limit(i);
		fontRuns.add(limit, &fonts[i]);
	}

	for (auto _ : state) {
		Text::LayoutInfo layoutInfo;
		Text::build_layout_info_utf8(layoutInfo, str.data(), str.size(), fontRuns, 100.f, 100.f,
				TextYAlignment::TOP, Text::LayoutInfoFlags::NONE);
		benchmark::DoNotOptimize(layoutInfo);
	}
}*/

BENCHMARK(BM_Layout_SingleFont_LineBreak);
BENCHMARK(BM_Layout_SingleFont_NoLineBreak);
//BENCHMARK(BM_Layout_MultiFont_LineBreak);

// Static Functions

static size_t apply_lang(std::default_random_engine& rng,
		std::span<const Text::Pair<uint32_t, uint32_t>> blockRanges, char* str, size_t count) {
	std::uniform_int_distribution<size_t> distBlocks(0, blockRanges.size() - 1);
	size_t offset{};
	bool error = false;

	auto [blkStart, blkEnd] = blockRanges[distBlocks(rng)];
	std::uniform_int_distribution<uint32_t> distChars(blkStart, blkEnd);

	for (size_t i = 0; !error && i < count; ++i) {
		auto chr = distChars(rng);

		U8_APPEND((uint8_t*)str, offset, count, chr, error);
	}

	return offset;
}

static void init_test_string() {
	if (g_testString) {
		return;
	}

	g_testString = std::make_unique_for_overwrite<char[]>(TEST_STRING_SIZE);
	g_testStringSize = 0;

	std::default_random_engine rng;
	std::normal_distribution distWordSize(WORD_SIZE_AVERAGE, WORD_SIZE_STDDEV);
	std::normal_distribution distParaSize(PARA_SIZE_AVERAGE, PARA_SIZE_STDDEV);
	std::uniform_int_distribution<size_t> distLangSelect(0, std::ssize(g_unicodeLangs) - 1);

	auto nextParaEnd = static_cast<size_t>(std::max(distParaSize(rng), 1.0));

	while (g_testStringSize < TEST_STRING_SIZE) {
		auto wordSize = static_cast<size_t>(std::min(std::max(distWordSize(rng), 1.0), 100.0));
		wordSize = std::min(wordSize, TEST_STRING_SIZE - g_testStringSize);
		auto& lang = g_unicodeLangs[distLangSelect(rng)];

		wordSize = apply_lang(rng, lang, g_testString.get() + g_testStringSize, wordSize);

		g_testStringSize += wordSize;

		if (g_testStringSize < TEST_STRING_SIZE) {
			uint32_t chr;
			bool error = false;

			if (g_testStringSize >= nextParaEnd) {
				std::uniform_int_distribution<size_t> distBreak(0, std::ssize(g_paragraphSeparators) - 1);
				chr = g_paragraphSeparators[distBreak(rng)];
			}
			else {
				std::uniform_int_distribution<size_t> distWhitespace(0, std::ssize(g_whitespace) - 1);
				chr = g_whitespace[distWhitespace(rng)];
			}

			U8_APPEND((uint8_t*)g_testString.get(), g_testStringSize, TEST_STRING_SIZE, chr, error);

			if (error) {
				break;
			}

			if (g_testStringSize >= nextParaEnd) {
				nextParaEnd = g_testStringSize + static_cast<size_t>(std::max(distParaSize(rng), 1.0));
			}
		}
	}

	size_t weightStyleIndex = 0;
	std::uniform_int_distribution<size_t> distWeightStyleLength(5, 150);
	std::uniform_int_distribution<uint32_t> distWeight(0, (uint32_t)FontWeightIndex::COUNT - 1);
	std::uniform_int_distribution<uint32_t> distStyle(0, 1);
	std::uniform_int_distribution<uint32_t> distSize(9, 32);

	while (weightStyleIndex < g_testStringSize) {
		weightStyleIndex += distWeightStyleLength(rng);
		weightStyleIndex = std::min(g_testStringSize, weightStyleIndex);

		auto weight = distWeight(rng);
		auto style = distStyle(rng);
		auto size = distSize(rng);

		auto fontInfo = (size << 16) | (weight << 1) | style;

		g_fontStyles.add(weightStyleIndex, fontInfo);
	}
}

static void dump_test_data() {
	// HTML
	{
		FILE* file = fopen("out_testdata.html", "wb");
		fputs("<html>\n\t<body>\n\t\t", file);
		size_t start = 0;

		for (size_t i = 0; i < g_fontStyles.get_run_count(); ++i) {
			auto limit = g_fontStyles.get_run_limit(i);
			auto value = g_fontStyles.get_run_value(i);
			auto weight = ((value >> 1) & 0xFF) * 100 + 100;
			bool italic = value & 1;
			auto size = value >> 16;

			UChar32 chr{};

			fprintf(file, "<span style=\"font-weight: %u; font-style: %s; font-size: %upx;\">", weight,
					italic ? "italic" : "normal", size);

			for (;;) {
				U8_NEXT((const uint8_t*)g_testString.get(), start, limit, chr);

				if (chr < 0) {
					break;
				}

				fprintf(file, "&#%d;", chr);

				if (start >= limit) {
					break;
				}
			}

			fputs("</span>", file);
		}

		fputs("\n\t</body>\n</html>", file);
		fclose(file);
	}

	// Plaintext
	{
		FILE* file = fopen("out_testdata.txt", "wb");
		fwrite(g_testString.get(), 1, g_testStringSize, file);
		fclose(file);
	}
}

