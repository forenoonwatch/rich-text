#include <catch2/catch_test_macros.hpp>

#include <font_cache.hpp>
#include <paragraph_layout.hpp>

#include <unicode/unistr.h>

#include <cmath>

static std::unique_ptr<FontCache> g_fontCache{};

static constexpr const char* g_testStrings[] = {
	"HelloWorld",
	"إلابسم الله",
	"beffiإلابسم اللهffter",
	"Hello\nWorld",
	"beffiإلابسم اللهffter\r\nbeffiإلابسم اللهffter",
	"beffiإلابسم الله\r\nhello",
};

static void init_font_cache();

static void test_lx_vs_icu(const MultiScriptFont& font, const char* str, float width);
static void test_lx_vs_utf8(const MultiScriptFont& font, const char* str, float width);
static void test_compare_layouts(const ParagraphLayout& lxLayout, const ParagraphLayout& icuLayout);

TEST_CASE("ICU UTF-16", "[ParagraphLayout]") {
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

TEST_CASE("ICU UTF-8", "[ParagraphLayout]") {
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
	RichText::TextRuns<const MultiScriptFont*> fontRuns(&font, text.length());

	ParagraphLayout lxLayout{};
	build_paragraph_layout_icu_lx(lxLayout, text.getBuffer(), text.length(), fontRuns, width, 100.f,
			TextYAlignment::BOTTOM, ParagraphLayoutFlags::NONE);

	LayoutBuildState state{};
	ParagraphLayout icuLayout{};
	build_paragraph_layout_icu(state, icuLayout, text.getBuffer(), text.length(), fontRuns, width, 100.f,
			TextYAlignment::BOTTOM, ParagraphLayoutFlags::NONE);
	layout_build_state_destroy(state);

	test_compare_layouts(lxLayout, icuLayout);
}

static void test_lx_vs_utf8(const MultiScriptFont& font, const char* str, float width) {
	icu::UnicodeString text(str);
	auto count = strlen(str);
	RichText::TextRuns<const MultiScriptFont*> fontRuns8(&font, count);
	RichText::TextRuns<const MultiScriptFont*> fontRuns16(&font, text.length());

	ParagraphLayout lxLayout{};
	build_paragraph_layout_icu_lx(lxLayout, text.getBuffer(), text.length(), fontRuns16, width, 100.f,
			TextYAlignment::BOTTOM, ParagraphLayoutFlags::NONE);
	convert_paragraph_layout_to_utf8(lxLayout, text.getBuffer(), text.length(), str, count);

	LayoutBuildState state{};
	ParagraphLayout utf8Layout{};
	build_paragraph_layout_utf8(state, utf8Layout, str, count, fontRuns8, width, 100.f, TextYAlignment::BOTTOM,
			ParagraphLayoutFlags::NONE);
	layout_build_state_destroy(state);

	test_compare_layouts(lxLayout, utf8Layout);
}

static void test_compare_layouts(const ParagraphLayout& lxLayout, const ParagraphLayout& icuLayout) {
	REQUIRE(lxLayout.glyphs.size() == icuLayout.glyphs.size());
	REQUIRE(lxLayout.charIndices.size() == icuLayout.charIndices.size());
	REQUIRE(lxLayout.glyphPositions.size() == icuLayout.glyphPositions.size());
	REQUIRE(lxLayout.visualRuns.size() == icuLayout.visualRuns.size());
	REQUIRE(lxLayout.lines.size() == icuLayout.lines.size());

	for (size_t i = 0; i < lxLayout.glyphs.size(); ++i) {
		REQUIRE(lxLayout.glyphs[i] == icuLayout.glyphs[i]);
	}

	for (size_t i = 0; i < lxLayout.charIndices.size(); ++i) {
		REQUIRE(lxLayout.charIndices[i] == icuLayout.charIndices[i]);
	}

	for (size_t i = 0; i < lxLayout.glyphPositions.size(); ++i) {
		REQUIRE(fabsf(lxLayout.glyphPositions[i] - icuLayout.glyphPositions[i]) < 0.01f);
	}

	for (size_t i = 0; i < lxLayout.visualRuns.size(); ++i) {
		REQUIRE(lxLayout.visualRuns[i].pFont == icuLayout.visualRuns[i].pFont);
		REQUIRE(lxLayout.visualRuns[i].glyphEndIndex == icuLayout.visualRuns[i].glyphEndIndex);
		REQUIRE(lxLayout.visualRuns[i].rightToLeft == icuLayout.visualRuns[i].rightToLeft);
	}

	for (size_t i = 0; i < lxLayout.lines.size(); ++i) {
		REQUIRE(lxLayout.lines[i].visualRunsEndIndex == icuLayout.lines[i].visualRunsEndIndex);
		REQUIRE(lxLayout.lines[i].lastStringIndex == icuLayout.lines[i].lastStringIndex);
		// LayoutEx truncates width to an integer, so there is some discrepancy
		REQUIRE(fabsf(lxLayout.lines[i].width - icuLayout.lines[i].width) < 1.f);
		REQUIRE(lxLayout.lines[i].ascent == icuLayout.lines[i].ascent);
		REQUIRE(lxLayout.lines[i].totalDescent == icuLayout.lines[i].totalDescent);
		REQUIRE(lxLayout.lines[i].lastCharDiff == icuLayout.lines[i].lastCharDiff);
	}

	REQUIRE(lxLayout.textStartY == icuLayout.textStartY);
	REQUIRE(lxLayout.rightToLeft == icuLayout.rightToLeft);
}

