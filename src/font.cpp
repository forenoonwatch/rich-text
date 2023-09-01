#include "font.hpp"

#include "font_cache.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_TRUETYPE_TABLES_H

#include <hb.h>

#include <layout/LEScripts.h>

Font::Font(FontCache& cache, FT_FaceRec_* ftFace, hb_font_t* hbFont, FaceIndex_T face, FamilyIndex_T family,
			FontWeightIndex weight, FontFaceStyle style, uint32_t size)
		: m_ftFace(ftFace)
		, m_hbFont(hbFont)
		, m_fontCache(&cache)
		, m_fontKey(static_cast<uint64_t>(style)
				| (static_cast<uint64_t>(weight) << 1)
				| (static_cast<uint64_t>(family) << 32)
				| (static_cast<uint64_t>(face) << 48)) 
		, m_size(size)
		, m_strikethroughPosition{}
		, m_strikethroughThickness{1} {
	if (auto* pOS2Table = reinterpret_cast<TT_OS2*>(FT_Get_Sfnt_Table(ftFace, FT_SFNT_OS2))) {
		m_strikethroughPosition = -pOS2Table->yStrikeoutPosition;
		m_strikethroughThickness = pOS2Table->yStrikeoutSize;
	}
}

Font::~Font() {
	hb_font_destroy(m_hbFont);
	FT_Done_Face(m_ftFace);
}

FontGlyphResult Font::get_glyph(uint32_t glyphIndex, float* offsetOut) const {
	FT_Load_Glyph(m_ftFace, glyphIndex, FT_LOAD_RENDER | FT_LOAD_COLOR);
	auto uWidth = static_cast<uint32_t>(m_ftFace->glyph->bitmap.width);
	auto uHeight = static_cast<uint32_t>(m_ftFace->glyph->bitmap.rows);

	FontGlyphResult result{
		.bitmap = Bitmap{uWidth, uHeight},
		.hasColor = false,
	};
	auto* buffer = m_ftFace->glyph->bitmap.buffer;

	switch (m_ftFace->glyph->bitmap.pixel_mode) {
		case FT_PIXEL_MODE_GRAY:
			for (uint32_t y = 0, i = 0; y < uHeight; ++y) {
				for (uint32_t x = 0; x < uWidth; ++x, ++i) {
					auto alpha = static_cast<float>(buffer[i]) / 255.f;
					result.bitmap.set_pixel(x, y, {1.f, 1.f, 1.f, alpha});
				}
			}
			break;
		case FT_PIXEL_MODE_BGRA:
			for (uint32_t y = 0, i = 0; y < uHeight; ++y) {
				for (uint32_t x = 0; x < uWidth; ++x, i += 4) {
					auto b = static_cast<float>(buffer[i]) / 255.f;
					auto g = static_cast<float>(buffer[i + 1]) / 255.f;
					auto r = static_cast<float>(buffer[i + 2]) / 255.f;
					auto a = static_cast<float>(buffer[i + 3]) / 255.f;
					result.bitmap.set_pixel(x, y, {r / a, g / a, b / a, a});
				}
			}

			result.hasColor = true;
			break;
		default:
			break;
	}

	offsetOut[0] = static_cast<float>(m_ftFace->glyph->bitmap_left);
	offsetOut[1] = static_cast<float>(-m_ftFace->glyph->bitmap_top);

	return result;
}

float Font::get_underline_position() const {
	return getScaleFactorY() * static_cast<float>(-m_ftFace->underline_position);
}

float Font::get_underline_thickness() const {
	return getScaleFactorY() * static_cast<float>(m_ftFace->underline_thickness);
}

float Font::get_strikethrough_position() const {
	return getScaleFactorY() * static_cast<float>(m_strikethroughPosition);
}

float Font::get_strikethrough_thickness() const {
	return getScaleFactorY() * static_cast<float>(m_strikethroughThickness);
}

const void* Font::getFontTable(LETag tableTag, size_t& length) const {
	if (auto* blob = hb_face_reference_table(hb_font_get_face(m_hbFont), tableTag)) {
		unsigned tmpLength{};
		auto* result = hb_blob_get_data(blob, &tmpLength);
		length = static_cast<size_t>(tmpLength);
		return result;
	}

	return nullptr;
}

le_int32 Font::getUnitsPerEM() const {
	return m_ftFace->units_per_EM;
}

LEGlyphID Font::mapCharToGlyph(LEUnicode32 ch) const {
	if (hb_codepoint_t result{}; hb_font_get_nominal_glyph(m_hbFont, ch, &result)) {
		return result;
	}

	return 0;
}

void Font::getGlyphAdvance(LEGlyphID glyph, LEPoint& advance) const {
	auto hAdv = hb_font_get_glyph_h_advance(m_hbFont, glyph);
	auto vAdv = hb_font_get_glyph_v_advance(m_hbFont, glyph);

	advance.fX = static_cast<float>(hAdv) / 64.f;
	advance.fY = static_cast<float>(vAdv) / 64.f;
}

le_bool Font::getGlyphPoint(LEGlyphID /* glyph */, le_int32 /* pointNumber */, LEPoint& /* point */) const {
	return false;
}

float Font::getXPixelsPerEm() const {
	return static_cast<float>(m_ftFace->size->metrics.x_ppem);
}

float Font::getYPixelsPerEm() const {
	return static_cast<float>(m_ftFace->size->metrics.y_ppem);
}

float Font::getScaleFactorX() const {
	return 1.f;
}

float Font::getScaleFactorY() const {
	return 1.f;
}

le_int32 Font::getAscent() const {
	return m_ftFace->size->metrics.ascender / 64;
}

le_int32 Font::getDescent() const {
	return -m_ftFace->size->metrics.descender / 64;
}

le_int32 Font::getLeading() const {
	return 0;
}

FontCache* Font::get_font_cache() const {
	return m_fontCache;
}

FaceIndex_T Font::get_face() const {
	return static_cast<FaceIndex_T>((m_fontKey >> 48) & 0xFFFFu);
}

FamilyIndex_T Font::get_family() const {
	return static_cast<FamilyIndex_T>((m_fontKey >> 32) & 0xFFFFu);
}

FontWeightIndex Font::get_weight() const {
	return static_cast<FontWeightIndex>((m_fontKey >> 1) & 0xFu);
}

FontFaceStyle Font::get_style() const {
	return static_cast<FontFaceStyle>(m_fontKey & 0x1u);
}

uint32_t Font::get_size() const {
	return m_size;
}

