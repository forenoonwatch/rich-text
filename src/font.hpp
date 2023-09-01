#pragma once

#include "bitmap.hpp"
#include "font_common.hpp"

#include <layout/LEFontInstance.h>

struct FT_FaceRec_;
struct hb_font_t;

class FontCache;

struct FontGlyphResult {
	Bitmap bitmap;
	bool hasColor;
};

class Font final : public icu::LEFontInstance {
	public:
		explicit Font(FontCache&, FT_FaceRec_*, hb_font_t*, FaceIndex_T, FamilyIndex_T, FontWeightIndex,
				FontFaceStyle, uint32_t size);
		~Font();

		FontGlyphResult get_glyph(uint32_t glyphIndex, float* offsetOut) const;

		float get_underline_position() const;
		float get_underline_thickness() const;

		float get_strikethrough_position() const;
		float get_strikethrough_thickness() const;

		const void* getFontTable(LETag tableTag, size_t &length) const override;
		le_int32 getUnitsPerEM() const override;
		LEGlyphID mapCharToGlyph(LEUnicode32 ch) const override;
		void getGlyphAdvance(LEGlyphID glyph, LEPoint &advance) const override;
		le_bool getGlyphPoint(LEGlyphID glyph, le_int32 pointNumber, LEPoint &point) const override;
		float getXPixelsPerEm() const override;
		float getYPixelsPerEm() const override;
		float getScaleFactorX() const override;
		float getScaleFactorY() const override;
		le_int32 getAscent() const override;
		le_int32 getDescent() const override;
		le_int32 getLeading() const override;

		FontCache* get_font_cache() const;

		FaceIndex_T get_face() const;
		FamilyIndex_T get_family() const;
		FontWeightIndex get_weight() const;
		FontFaceStyle get_style() const;
		uint32_t get_size() const;
	private:
		FT_FaceRec_* m_ftFace;
		hb_font_t* m_hbFont;
		FontCache* m_fontCache;
		uint64_t m_fontKey;
		uint32_t m_size;
		/**
		 * https://learn.microsoft.com/en-us/typography/opentype/otspec182/os2#ystrikeoutposition
		 * The position of the top of the strikeout stroke relative to the baseline in font design units.
		 * Positive values represent distances above the baseline, while negative values represent distances
		 * below the baseline... For a Roman font with a 2048 em square, 460 is suggested.
		 */
		int16_t m_strikethroughPosition;
		/**
		 * https://learn.microsoft.com/en-us/typography/opentype/otspec182/os2#ystrikeoutsize
		 * Width of the strikeout stroke in font design units.
		 * This field should normally be the width of the em dash for the current font. If the size is one, the
		 * strikeout line will be the line represented by the strikeout position field. If the value is two, the 
		 * strikeout line will be the line represented by the strikeout position and the line immediately above 
		 * the strikeout position. For a Roman font with a 2048 em square, 102 is suggested.
		 */
		int16_t m_strikethroughThickness;
};

