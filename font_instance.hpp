#pragma once

#include <layout/LEFontInstance.h>

#include <memory>

struct FT_FaceRec_;
struct hb_font_t;

class FontInstance final : public icu::LEFontInstance {
	public:
		static std::unique_ptr<FontInstance> create(const char* fileName);

		explicit FontInstance(struct FT_FaceRec_* ftFace, hb_font_t* hbFont, std::unique_ptr<char[]>&& fileData,
				size_t fileSize);
		~FontInstance();

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
	private:
		struct FT_FaceRec_* m_ftFace;
		hb_font_t* m_hbFont;
		std::unique_ptr<char[]> m_fileData;
		size_t m_fileSize;
};

