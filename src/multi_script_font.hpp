#pragma once

#include "font_common.hpp"

#include <layout/LEFontInstance.h>

class FontCache;

class MultiScriptFont final : public icu::LEFontInstance {
	public:
		MultiScriptFont() = default;
		explicit MultiScriptFont(FontCache&, FamilyIndex_T, FontWeightIndex, FontFaceStyle, uint32_t size);

		explicit operator bool() const;

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

		FontCache* get_font_cache() const;

		FamilyIndex_T get_family() const;
		FontWeightIndex get_weight() const;
		FontFaceStyle get_style() const;
		uint32_t get_size() const;
	private:
		FontCache* m_fontCache{};
		uint32_t m_fontKey;
		uint32_t m_size;
};

