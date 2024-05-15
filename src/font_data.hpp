#pragma once

#include "bitmap.hpp"
#include "font_common.hpp"
#include "stroke_type.hpp"

struct FT_FaceRec_;
struct hb_font_t;

namespace Text {

constexpr const double MSDF_PIXELS_PER_EM = 32.0;

struct FontGlyphResult {
	Bitmap bitmap;
	bool hasColor;
};

struct FontData {
	FT_FaceRec_* ftFace;
	hb_font_t* hbFont;

	/**
	 * https://learn.microsoft.com/en-us/typography/opentype/otspec182/os2#ystrikeoutposition
	 * The position of the top of the strikeout stroke relative to the baseline in font design units.
	 * Positive values represent distances above the baseline, while negative values represent distances
	 * below the baseline... For a Roman font with a 2048 em square, 460 is suggested.
	 */
	int16_t strikethroughPosition;
	/**
	 * https://learn.microsoft.com/en-us/typography/opentype/otspec182/os2#ystrikeoutsize
	 * Width of the strikeout stroke in font design units.
	 * This field should normally be the width of the em dash for the current font. If the size is one, the
	 * strikeout line will be the line represented by the strikeout position field. If the value is two, the 
	 * strikeout line will be the line represented by the strikeout position and the line immediately above 
	 * the strikeout position. For a Roman font with a 2048 em square, 102 is suggested.
	 */
	int16_t strikethroughThickness;

	SyntheticFontInfo synthInfo;

	constexpr bool valid() const {
		return ftFace && hbFont;
	}

	constexpr explicit operator bool() const {
		return valid();
	}

	float get_ascent() const;
	float get_descent() const;

	uint32_t get_upem() const;
	float get_ppem_x() const;
	float get_ppem_y() const;

	float get_scale_x() const;
	float get_scale_y() const;

	bool has_codepoint(uint32_t codepoint) const;
	uint32_t map_codepoint_to_glyph(uint32_t codepoint) const;

	float get_glyph_advance_x(uint32_t glyph) const;
	float get_glyph_advance_y(uint32_t glyph) const;

	float get_underline_position() const;
	float get_underline_thickness() const;

	float get_strikethrough_position() const;
	float get_strikethrough_thickness() const;

	FontGlyphResult rasterize_glyph(uint32_t glyph, float* offsetOut) const;
	FontGlyphResult rasterize_glyph_outline(uint32_t glyph, uint8_t thickness, StrokeType,
			float* offsetOut) const;

	Bitmap get_msdf_glyph(uint32_t glyphIndex, float* offsetOut) const;
	Bitmap get_msdf_outline_glyph(uint32_t glyphIndex, uint8_t thickness, StrokeType,
			float* offsetOut) const;
};

}

