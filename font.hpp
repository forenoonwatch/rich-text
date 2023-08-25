#pragma once

#include "bitmap.hpp"
#include "font_common.hpp"

#include <layout/LEFontInstance.h>

struct FT_FaceRec_;
struct hb_font_t;

class FontCache;

class Font final : public icu::LEFontInstance {
	public:
		explicit Font(FontCache&, FT_FaceRec_*, hb_font_t*, FaceIndex_T, FamilyIndex_T, FontWeightIndex,
				FontFaceStyle, uint32_t size);
		~Font();

		Bitmap get_glyph(uint32_t glyphIndex, float* offsetOut) const;

		float get_baseline() const;
		float get_line_height() const;

		const LEFontInstance* getSubFont(const LEUnicode chars[], le_int32* offset, le_int32 limit,
				le_int32 script, LEErrorCode& success) const override;

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

		FaceIndex_T get_face() const;
		FamilyIndex_T get_family() const;
		FontWeightIndex get_weight() const;
		FontFaceStyle get_style() const;
	private:
		FT_FaceRec_* m_ftFace;
		hb_font_t* m_hbFont;
		FontCache* m_fontCache;
		uint64_t m_fontKey;
		uint32_t m_size;
};

