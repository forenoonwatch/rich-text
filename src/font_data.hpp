#pragma once

#include "bitmap.hpp"
#include "font_common.hpp"
#include "stroke_type.hpp"

struct FT_FaceRec_;
struct FT_GlyphRec_;
struct FT_Outline_;
struct FT_StrokerRec_;
struct hb_font_t;

namespace Text {

enum class FontRasterFormat : uint8_t {
	INVALID,
	R8,
	BGRA8,
};

struct FontRasterizeInfo {
	const std::byte* pData;
	float offsetX;
	float offsetY;
	uint32_t width;
	uint32_t height;
	FontRasterFormat format;
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

	/**
	 * Rasterizes the given glyph and passes the relevant data to the functor `func`. The `pData` member
	 * of the `FontRasterizeInfo` is only valid for the duration of the call to `func` and is freed
	 * immediately after execution is complete.
	 */
	template <typename Functor>
	void rasterize_glyph(uint32_t glyph, Functor&& func) const {
		func(rasterize_glyph_internal(glyph));
	}

	/**
	 * Rasterizes the given glyph outline and passes the relevant data to the functor `func`. The `pData` member
	 * of the `FontRasterizeInfo` is only valid for the duration of the call to `func` and is freed
	 * immediately after execution is complete.
	 */
	template <typename Functor>
	void rasterize_glyph_outline(uint32_t glyph, uint8_t thickness, StrokeType strokeType,
			Functor&& func) const {
		FT_StrokerRec_* pStroker;
		FT_GlyphRec_* pGlyph;
		func(rasterize_outline_internal(glyph, thickness, strokeType, pStroker, pGlyph));
		rasterize_outline_finish(pStroker, pGlyph);
	}

	template <typename Functor>
	void load_glyph_curve(uint32_t glyphIndex, Functor&& func) const {
		func(*load_glyph_curve_internal(glyphIndex));
	}

	template <typename Functor>
	void load_glyph_outline_curve(uint32_t glyphIndex, uint8_t thickness, StrokeType strokeType,
			Functor&& func) const {
		FT_StrokerRec_* pStroker;
		FT_GlyphRec_* pGlyph;
		FT_Outline_* pOutline = load_outline_curve_internal(glyphIndex, thickness, strokeType, pStroker,
				pGlyph);
		func(*pOutline);
		outline_curve_finish(pOutline, pStroker, pGlyph);
	}

	private:
		FontRasterizeInfo rasterize_glyph_internal(uint32_t glyph) const;
		FontRasterizeInfo rasterize_outline_internal(uint32_t glyph, uint8_t thickness, StrokeType,
				FT_StrokerRec_*& outStroker, FT_GlyphRec_*& outGlyph) const;
		void rasterize_outline_finish(FT_StrokerRec_*, FT_GlyphRec_*) const;

		FT_Outline_* load_glyph_curve_internal(uint32_t glyphIndex) const;
		FT_Outline_* load_outline_curve_internal(uint32_t glyphIndex, uint8_t thickness, StrokeType,
				FT_StrokerRec_*&, FT_GlyphRec_*&) const;
		void outline_curve_finish(FT_Outline_*, FT_StrokerRec_*, FT_GlyphRec_*) const;
};

}

