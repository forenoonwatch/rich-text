#include "font_instance.hpp"

#include <cstdio>

#include <atomic>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <hb-ft.h>

#include "file_read_bytes.hpp"

static FT_Library g_freetype;
static std::atomic_int64_t g_instanceCount{};

std::unique_ptr<FontInstance> FontInstance::create(const char* fileName) {
	if (g_instanceCount.fetch_add(1, std::memory_order_relaxed) == 0) {
		FT_Init_FreeType(&g_freetype);
	}

	size_t fileSize{};
	if (auto fileData = file_read_bytes(fileName, fileSize)) {
		FT_Face face;
		if (FT_New_Memory_Face(g_freetype, reinterpret_cast<const FT_Byte*>(fileData.get()), fileSize, 0,
				&face) != 0) {
			return {};
		}

		hb_font_t* hbFont{};
		if (!(hbFont = hb_ft_font_create(face, nullptr))) {
			return {};
		}

		hb_ft_font_set_load_flags(hbFont, FT_LOAD_DEFAULT);

		return std::make_unique<FontInstance>(face, hbFont, std::move(fileData), fileSize);
	}

	return nullptr;
}

FontInstance::FontInstance(FT_Face ftFace, hb_font_t* hbFont, std::unique_ptr<char[]>&& fileData,
			size_t fileSize)
		: m_ftFace(ftFace)
		, m_hbFont(hbFont)
		, m_fileData(std::move(fileData))
		, m_fileSize(fileSize) {}

FontInstance::~FontInstance() {
	FT_Done_Face(m_ftFace);

	if (g_instanceCount.fetch_sub(1, std::memory_order_acq_rel) == 1) {
		FT_Done_FreeType(g_freetype);
	}
}

const void* FontInstance::getFontTable(LETag tableTag, size_t& length) const {
	if (auto* blob = hb_face_reference_table(hb_font_get_face(m_hbFont), tableTag)) {
		unsigned tmpLength{};
		auto* result = hb_blob_get_data(blob, &tmpLength);
		length = static_cast<size_t>(tmpLength);
		return result;
	}

	return nullptr;
}

le_int32 FontInstance::getUnitsPerEM() const {
	return hb_face_get_upem(hb_font_get_face(m_hbFont));
}

LEGlyphID FontInstance::mapCharToGlyph(LEUnicode32 ch) const {
	if (hb_codepoint_t result{}; hb_font_get_nominal_glyph(m_hbFont, ch, &result)) {
		return result;
	}

	return 0;
}

void FontInstance::getGlyphAdvance(LEGlyphID glyph, LEPoint& advance) const {
	auto hAdv = hb_font_get_glyph_h_advance(m_hbFont, glyph);
	auto vAdv = hb_font_get_glyph_v_advance(m_hbFont, glyph);

	advance.fX = static_cast<float>(hAdv) / 64.f;
	advance.fY = static_cast<float>(vAdv) / 64.f;
}

le_bool FontInstance::getGlyphPoint(LEGlyphID glyph, le_int32 pointNumber, LEPoint& point) const {
	hb_position_t x, y;
	if (!hb_font_get_glyph_contour_point(m_hbFont, glyph, pointNumber, &x, &y)) {
		return false;
	}

	point.fX = static_cast<float>(x) / 64.f;
	point.fY = static_cast<float>(y) / 64.f;

	return true;
}

float FontInstance::getXPixelsPerEm() const {
	unsigned xPPEM, yPPEM;
	hb_font_get_ppem(m_hbFont, &xPPEM, &yPPEM);
	return static_cast<float>(xPPEM) / 64.f;
}

float FontInstance::getYPixelsPerEm() const {
	unsigned xPPEM, yPPEM;
	hb_font_get_ppem(m_hbFont, &xPPEM, &yPPEM);
	return static_cast<float>(yPPEM) / 64.f;
}

float FontInstance::getScaleFactorX() const {
	int xScale, yScale;
	hb_font_get_scale(m_hbFont, &xScale, &yScale);
	return static_cast<float>(xScale) / 64.f;
}

float FontInstance::getScaleFactorY() const {
	int xScale, yScale;
	hb_font_get_scale(m_hbFont, &xScale, &yScale);
	return static_cast<float>(yScale) / 64.f;
}

le_int32 FontInstance::getAscent() const {
	hb_font_extents_t extents{};
	if (hb_font_get_v_extents(m_hbFont, &extents)) {
		return static_cast<float>(extents.ascender) / 64.f;
	}

	return 0;
}

le_int32 FontInstance::getDescent() const {
	hb_font_extents_t extents{};
	if (hb_font_get_v_extents(m_hbFont, &extents)) {
		return static_cast<float>(extents.descender) / 64.f;
	}

	return 0;
}

le_int32 FontInstance::getLeading() const {
	hb_font_extents_t extents{};
	if (hb_font_get_v_extents(m_hbFont, &extents)) {
		return static_cast<float>(extents.line_gap) / 64.f;
	}

	return 0;
}

