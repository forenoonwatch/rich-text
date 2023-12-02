#include <catch2/catch_test_macros.hpp>

#include <font_cache.hpp>
#include <layout_info.hpp>

#include <unicode/unistr.h>

#include <cmath>

static std::unique_ptr<FontCache> g_fontCache{};

static constexpr const char* g_testStrings[] = {
	"HelloWorld",
	"إلابسم الله",
	"beffiإلابسم اللهffter",
	"Hello\nWorld",
	"Hello\r\n\r\nWorld",
	"beffiإلابسم اللهffter\r\nbeffiإلابسم اللهffter",
	"beffiإلابسم الله\r\nhello",
	//"	 ̈‫ aaa ‭ אאא ‮ aaa ‪ אאא ‬ aaa ‬ ااا ‬ aaa ‬ aaa ‬&‬‌‌&‬",
	//"aaa\u2067אאא\u2066bbb\u202bבבב\u202cccc\u2069גגג",
};

static void init_font_cache();

static void test_lx_vs_icu(const MultiScriptFont& font, const char* str, float width);
static void test_lx_vs_utf8(const MultiScriptFont& font, const char* str, float width);
static void test_compare_layouts(const Text::LayoutInfo& lxLayout, const Text::LayoutInfo& icuLayout);

TEST_CASE("ICU UTF-16", "[LayoutInfo]") {
	init_font_cache();
	auto famIdx = g_fontCache->get_font_family("Noto Sans"); 
	auto font = g_fontCache->get_font(famIdx, FontWeightIndex::REGULAR, FontFaceStyle::NORMAL, 48);

	SECTION("Single Font Softbreaking") {
		for (size_t i = 0; i < std::ssize(g_testStrings); ++i) {
			test_lx_vs_icu(font, g_testStrings[i], 100.f);
		}
	}

	SECTION("Single Font No Softbreaking") {
		for (size_t i = 0; i < std::ssize(g_testStrings); ++i) {
			test_lx_vs_icu(font, g_testStrings[i], 0.f);
		}
	}
}

TEST_CASE("ICU UTF-8", "[LayoutInfo]") {
	init_font_cache();
	auto famIdx = g_fontCache->get_font_family("Noto Sans"); 
	auto font = g_fontCache->get_font(famIdx, FontWeightIndex::REGULAR, FontFaceStyle::NORMAL, 48);

	SECTION("Single Font Softbreaking") {
		for (size_t i = 0; i < std::ssize(g_testStrings); ++i) {
			test_lx_vs_utf8(font, g_testStrings[i], 100.f);
		}
	}

	SECTION("Single Font No Softbreaking") {
		for (size_t i = 0; i < std::ssize(g_testStrings); ++i) {
			test_lx_vs_utf8(font, g_testStrings[i], 0.f);
		}
	}
}

// Static Functions

static void init_font_cache() {
	if (!g_fontCache) {
		g_fontCache = std::make_unique<FontCache>("fonts/families");
	}

	REQUIRE(g_fontCache);
}

static void test_lx_vs_icu(const MultiScriptFont& font, const char* str, float width) {
	icu::UnicodeString text(str);
	Text::ValueRuns<const MultiScriptFont*> fontRuns(&font, text.length());

	Text::LayoutInfo lxLayout{};
	build_layout_info_icu_lx(lxLayout, text.getBuffer(), text.length(), fontRuns, width, 100.f,
			TextYAlignment::BOTTOM, Text::LayoutInfoFlags::NONE);

	Text::LayoutInfo icuLayout{};
	build_layout_info_icu(icuLayout, text.getBuffer(), text.length(), fontRuns, width, 100.f,
			TextYAlignment::BOTTOM, Text::LayoutInfoFlags::NONE);

	test_compare_layouts(lxLayout, icuLayout);
}

static void test_lx_vs_utf8(const MultiScriptFont& font, const char* str, float width) {
	icu::UnicodeString text(str);
	auto count = strlen(str);
	Text::ValueRuns<const MultiScriptFont*> fontRuns8(&font, count);
	Text::ValueRuns<const MultiScriptFont*> fontRuns16(&font, text.length());

	Text::LayoutInfo lxLayout{};
	Text::LayoutInfo lxLayout8{};
	build_layout_info_icu_lx(lxLayout, text.getBuffer(), text.length(), fontRuns16, width, 100.f,
			TextYAlignment::BOTTOM, Text::LayoutInfoFlags::NONE);
	convert_layout_info_to_utf8(lxLayout, lxLayout8, text.getBuffer(), text.length(), str, count);

	Text::LayoutInfo utf8Layout{};
	build_layout_info_utf8(utf8Layout, str, count, fontRuns8, width, 100.f, TextYAlignment::BOTTOM,
			Text::LayoutInfoFlags::NONE);

	test_compare_layouts(lxLayout8, utf8Layout);
}

static void test_compare_layouts(const Text::LayoutInfo& lxLayout, const Text::LayoutInfo& icuLayout) {
	REQUIRE(lxLayout.get_run_count() == icuLayout.get_run_count());
	REQUIRE(lxLayout.get_glyph_count() == icuLayout.get_glyph_count());
	REQUIRE(lxLayout.get_char_index_count() == icuLayout.get_char_index_count());
	REQUIRE(lxLayout.get_glyph_position_data_count() == icuLayout.get_glyph_position_data_count());
	REQUIRE(lxLayout.get_line_count() == icuLayout.get_line_count());

	for (size_t i = 0; i < lxLayout.get_glyph_count(); ++i) {
		REQUIRE(lxLayout.get_glyph_id(i) == icuLayout.get_glyph_id(i));
	}

	for (size_t i = 0; i < lxLayout.get_char_index_count(); ++i) {
		REQUIRE(lxLayout.get_char_index(i) == icuLayout.get_char_index(i));
	}

	auto* lxPosData = lxLayout.get_glyph_position_data();
	auto* icuPosData = icuLayout.get_glyph_position_data();
	for (size_t i = 0; i < lxLayout.get_glyph_position_data_count(); ++i) {
		REQUIRE(fabsf(lxPosData[i] - icuPosData[i]) < 0.01f);
	}

	for (size_t i = 0; i < lxLayout.get_run_count(); ++i) {
		REQUIRE(lxLayout.get_run_font(i) == icuLayout.get_run_font(i));
		REQUIRE(lxLayout.get_run_glyph_end_index(i) == icuLayout.get_run_glyph_end_index(i));
		REQUIRE(lxLayout.is_run_rtl(i) == icuLayout.is_run_rtl(i));
		REQUIRE(lxLayout.get_run_char_start_index(i) == icuLayout.get_run_char_start_index(i));
		REQUIRE(lxLayout.get_run_char_end_index(i) == icuLayout.get_run_char_end_index(i));
		REQUIRE(lxLayout.get_run_char_end_offset(i) == icuLayout.get_run_char_end_offset(i));
	}

	for (size_t i = 0; i < lxLayout.get_line_count(); ++i) {
		REQUIRE(lxLayout.get_line_run_end_index(i) == icuLayout.get_line_run_end_index(i));
		// LayoutEx truncates width to an integer, so there is some discrepancy
		REQUIRE(fabsf(lxLayout.get_line_width(i) - icuLayout.get_line_width(i)) < 1.f);
		REQUIRE(lxLayout.get_line_ascent(i) == icuLayout.get_line_ascent(i));
		REQUIRE(lxLayout.get_line_total_descent(i) == icuLayout.get_line_total_descent(i));
	}

	REQUIRE(lxLayout.get_text_start_y() == icuLayout.get_text_start_y());
}

