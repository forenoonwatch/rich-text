#include "layout_info.hpp"
#include "utf_conversion_util.hpp"

using namespace Text;

static void convert_char_indices(const LayoutInfo& src, LayoutInfo& dst, const char16_t* srcChars,
		int32_t srcCharCount, const char* dstChars, int32_t dstCharCount);
static void convert_runs(const LayoutInfo& src, LayoutInfo& dst, const char16_t* srcChars,
		int32_t srcCharCount, const char* dstChars, int32_t dstCharCount);

// Public Functions

void Text::convert_layout_info_to_utf8(const LayoutInfo& src, LayoutInfo& result, const char16_t* srcChars,
		int32_t srcCharCount, const char* dstChars, int32_t dstCharCount) {
	result.clear();
	result.reserve_runs(src.get_run_count());

	auto* glyphPositions = src.get_glyph_position_data();
	for (size_t i = 0; i < src.get_glyph_position_data_count(); i += 2) {
		result.append_glyph_position(glyphPositions[i], glyphPositions[i + 1]);
	}

	convert_char_indices(src, result, srcChars, srcCharCount, dstChars, dstCharCount);
	convert_runs(src, result, srcChars, srcCharCount, dstChars, dstCharCount);

	result.set_text_start_y(src.get_text_start_y());
}

// Static Functions

static void convert_char_indices(const LayoutInfo& src, LayoutInfo& dst, const char16_t* srcChars,
		int32_t srcCharCount, const char* dstChars, int32_t dstCharCount) {
	for (size_t i = 0; i < src.get_glyph_count(); ++i) {
		dst.append_char_index(utf16_index_to_utf8(srcChars, srcCharCount, dstChars, dstCharCount,
				src.get_char_index(static_cast<uint32_t>(i))));
	}
}

static void convert_runs(const LayoutInfo& src, LayoutInfo& dst, const char16_t* srcChars,
		int32_t srcCharCount, const char* dstChars, int32_t dstCharCount) {
	for (size_t line = 0, run = 0; line < src.get_line_count(); ++line) {
		for (; run < src.get_line_run_end_index(line); ++run) {
			for (auto glyph = src.get_first_glyph_index(run); glyph < src.get_run_glyph_end_index(run); ++glyph) {
				dst.append_glyph(src.get_glyph_id(glyph));
			}

			auto highChar = utf16_index_to_utf8(srcChars, srcCharCount, dstChars, dstCharCount,
					src.get_run_char_end_index(run) + src.get_run_char_end_offset(run));
			auto lowChar = utf16_index_to_utf8(srcChars, srcCharCount, dstChars, dstCharCount,
					src.get_run_char_end_index(run));
			auto charStartIndex = utf16_index_to_utf8(srcChars, srcCharCount, dstChars, dstCharCount,
					src.get_run_char_start_index(run));
			dst.append_run(src.get_run_font(run), charStartIndex, lowChar, src.is_run_rtl(run));
			dst.set_run_char_end_offset(run, highChar - lowChar);
		}

		dst.append_line(src.get_line_height(line), src.get_line_ascent(line));
	}
}

