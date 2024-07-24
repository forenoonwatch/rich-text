#include "font_data.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_STROKER_H

#include <hb.h>

#include <cmath>

using namespace Text;

static constexpr float BOLD_SCALE[] = {
	-1.f / 14.f, // Thin
	-1.f / 18.f, // Extra Light
	-1.f / 32.f, // Light
	0.f, // Regular
	1.f / 32.f, // Medium
	1.f / 18.f, // Semi Bold
	1.f / 14.f, // Bold
	1.f / 11.f, // Extra Bold
	1.f / 9.f, // Black
};

static constexpr float BOLD_SCALE_Y = 0.4f;

static constexpr const double M_PI = 3.14159265358979323846;
static constexpr const double ITALIC_SHEAR = 12.0  * M_PI / 180.0;

static void try_apply_synthetics(FT_Face face, FT_Outline& outline, SyntheticFontInfo synthInfo);

static void apply_synthetic_bold(FT_Face face, FT_Outline& outline, FontWeight srcWeight, FontWeight dstWeight);
static void apply_synthetic_italic(FT_Face face, FT_Outline& outline, FontStyle srcStyle, FontStyle dstStyle);

float FontData::get_ascent() const {
	return static_cast<float>(ftFace->size->metrics.ascender) / 64.f
			/ calc_font_scale_modifier(false, synthInfo.syntheticSubscript || synthInfo.syntheticSuperscript);
}

float FontData::get_descent() const {
	return static_cast<float>(ftFace->size->metrics.descender) / 64.f
			/ calc_font_scale_modifier(false, synthInfo.syntheticSubscript || synthInfo.syntheticSuperscript);
}

uint32_t FontData::get_upem() const {
	return ftFace->units_per_EM;
}

float FontData::get_ppem_x() const {
	return static_cast<float>(ftFace->size->metrics.x_ppem);
}

float FontData::get_ppem_y() const {
	return static_cast<float>(ftFace->size->metrics.y_ppem);
}

float FontData::get_scale_x() const {
	auto& metrics = ftFace->size->metrics;
	return static_cast<float>(metrics.x_ppem) / static_cast<float>(ftFace->units_per_EM);
}

float FontData::get_scale_y() const {
	auto& metrics = ftFace->size->metrics;
	return static_cast<float>(metrics.y_ppem) / static_cast<float>(ftFace->units_per_EM);
}

bool FontData::has_codepoint(uint32_t codepoint) const {
	hb_codepoint_t tmp;
	return hb_font_get_nominal_glyph(hbFont, codepoint, &tmp);
}

uint32_t FontData::map_codepoint_to_glyph(uint32_t codepoint) const {
	if (hb_codepoint_t result{}; hb_font_get_nominal_glyph(hbFont, codepoint, &result)) {
		return result;
	}

	return 0;
}

float FontData::get_glyph_advance_x(uint32_t glyph) const {
	return hb_font_get_glyph_h_advance(hbFont, glyph);
}

float FontData::get_glyph_advance_y(uint32_t glyph) const {
	return hb_font_get_glyph_v_advance(hbFont, glyph);
}

float FontData::get_underline_position() const {
	return get_scale_y() * static_cast<float>(-ftFace->underline_position);
}

float FontData::get_underline_thickness() const {
	return get_scale_y() * static_cast<float>(ftFace->underline_thickness);
}

float FontData::get_strikethrough_position() const {
	return get_scale_y() * static_cast<float>(strikethroughPosition);
}

float FontData::get_strikethrough_thickness() const {
	return get_scale_y() * static_cast<float>(strikethroughThickness);
}

FontRasterizeInfo FontData::rasterize_glyph_internal(uint32_t glyph) const {
	FT_Load_Glyph(ftFace, glyph, FT_LOAD_NO_BITMAP | FT_LOAD_COLOR);

	try_apply_synthetics(ftFace, ftFace->glyph->outline, synthInfo);

	FT_Render_Glyph(ftFace->glyph, FT_RENDER_MODE_NORMAL);

	auto uWidth = static_cast<uint32_t>(ftFace->glyph->bitmap.width);
	auto uHeight = static_cast<uint32_t>(ftFace->glyph->bitmap.rows);
	auto* buffer = ftFace->glyph->bitmap.buffer;

	FontRasterFormat format{FontRasterFormat::INVALID};

	switch (ftFace->glyph->bitmap.pixel_mode) {
		case FT_PIXEL_MODE_GRAY:
			format = FontRasterFormat::R8;
			break;
		case FT_PIXEL_MODE_BGRA:
			format = FontRasterFormat::BGRA8;
			break;
		default:
			break;
	}

	return {
		.pData = reinterpret_cast<const std::byte*>(buffer),
		.offsetX = static_cast<float>(ftFace->glyph->bitmap_left),
		.offsetY = static_cast<float>(-ftFace->glyph->bitmap_top),
		.width = uWidth,
		.height = uHeight,
		.format = format,
	};
}

FontRasterizeInfo FontData::rasterize_outline_internal(uint32_t glyphIndex, uint8_t thickness,
		StrokeType strokeType, FT_Stroker& outStroker, FT_Glyph& outGlyph) const {
	FT_Load_Glyph(ftFace, glyphIndex, FT_LOAD_NO_BITMAP);

	FT_Glyph glyph;
	FT_Get_Glyph(ftFace->glyph, &glyph);
	glyph->format = FT_GLYPH_FORMAT_OUTLINE;

	FT_Stroker_LineJoin lineJoin = FT_STROKER_LINEJOIN_ROUND;

	switch (strokeType) {
		case StrokeType::BEVEL:
			lineJoin = FT_STROKER_LINEJOIN_BEVEL;
			break;
		case StrokeType::MITER:
			lineJoin = FT_STROKER_LINEJOIN_MITER;
			break;
		default:
			break;
	}

	FT_Stroker stroker;
	FT_Stroker_New(glyph->library, &stroker);
	FT_Stroker_Set(stroker, static_cast<FT_Fixed>(thickness) * 64, FT_STROKER_LINECAP_ROUND, lineJoin, 0);

	FT_Glyph_Stroke(&glyph, stroker, false);

	try_apply_synthetics(ftFace, reinterpret_cast<FT_OutlineGlyph>(glyph)->outline, synthInfo);

	FT_Glyph_To_Bitmap(&glyph, FT_RENDER_MODE_NORMAL, nullptr, true);

	auto bmpGlyph = reinterpret_cast<FT_BitmapGlyph>(glyph);
	auto uWidth = static_cast<uint32_t>(bmpGlyph->bitmap.width);
	auto uHeight = static_cast<uint32_t>(bmpGlyph->bitmap.rows);
	auto* buffer = bmpGlyph->bitmap.buffer;

	outStroker = stroker;
	outGlyph = glyph;

	return {
		.pData = reinterpret_cast<const std::byte*>(buffer),
		.offsetX = static_cast<float>(bmpGlyph->left),
		.offsetY = static_cast<float>(-bmpGlyph->top),
		.width = uWidth,
		.height = uHeight,
		.format = FontRasterFormat::R8,
	};
}

void FontData::rasterize_outline_finish(FT_Stroker stroker, FT_Glyph glyph) const {
	FT_Stroker_Done(stroker);
	FT_Done_Glyph(glyph);
}

FT_Outline* FontData::load_glyph_curve_internal(uint32_t glyphIndex) const {
	FT_Load_Glyph(ftFace, glyphIndex, FT_LOAD_NO_BITMAP | FT_LOAD_NO_SCALE);
	try_apply_synthetics(ftFace, ftFace->glyph->outline, synthInfo);
	return &ftFace->glyph->outline;
}

FT_Outline* FontData::load_outline_curve_internal(uint32_t glyphIndex, uint8_t thickness,
		StrokeType type, FT_Stroker& outStroker, FT_Glyph& outGlyph) const {
	FT_Load_Glyph(ftFace, glyphIndex, FT_LOAD_NO_BITMAP | FT_LOAD_NO_SCALE);

	FT_Glyph glyph;
	FT_Get_Glyph(ftFace->glyph, &glyph);
	glyph->format = FT_GLYPH_FORMAT_OUTLINE;

	FT_Stroker_LineJoin lineJoin = FT_STROKER_LINEJOIN_ROUND;

	switch (type) {
		case StrokeType::BEVEL:
			lineJoin = FT_STROKER_LINEJOIN_BEVEL;
			break;
		case StrokeType::MITER:
			lineJoin = FT_STROKER_LINEJOIN_MITER;
			break;
		default:
			break;
	}

	FT_UInt points, contours;
	FT_Stroker stroker;
	FT_Stroker_New(glyph->library, &stroker);
	FT_Stroker_Set(stroker, static_cast<FT_Fixed>(thickness) * 64, FT_STROKER_LINECAP_ROUND, lineJoin, 0);

	FT_Glyph_Stroke(&glyph, stroker, false);
	FT_Stroker_GetCounts(stroker, &points, &contours);

	FT_Outline* pOutline = (FT_Outline*)malloc(sizeof(FT_Outline));
	FT_Outline_New(glyph->library, points, contours, pOutline);
	pOutline->n_points = 0;
	pOutline->n_contours = 0;

	FT_Stroker_Export(stroker, pOutline);

	try_apply_synthetics(ftFace, *pOutline, synthInfo);

	outStroker = stroker;
	outGlyph = glyph;

	return pOutline;
}

void FontData::outline_curve_finish(FT_Outline* outline, FT_Stroker stroker, FT_Glyph glyph) const {
	FT_Outline_Done(glyph->library, outline);
	FT_Stroker_Done(stroker);
	FT_Done_Glyph(glyph);
	free(outline);
}

// Static Functions

static void try_apply_synthetics(FT_Face face, FT_Outline& outline, SyntheticFontInfo synthInfo) {
	if (synthInfo.srcStyle != synthInfo.dstStyle) {
		apply_synthetic_italic(face, outline, synthInfo.srcStyle, synthInfo.dstStyle);
	}

	if (synthInfo.srcWeight != synthInfo.dstWeight) {
		apply_synthetic_bold(face, outline, synthInfo.srcWeight, synthInfo.dstWeight);
	}
}

static void apply_synthetic_bold(FT_Face face, FT_Outline& outline, FontWeight /*srcWeight*/,
		FontWeight dstWeight) {
	// FIXME: Create an effective scaling based on srcWeight
	auto dstWeightIndex = static_cast<size_t>(dstWeight);

	auto extraX = FT_MulFix(face->units_per_EM, face->size->metrics.x_scale) * BOLD_SCALE[dstWeightIndex];
	auto extraY = FT_MulFix(face->units_per_EM, face->size->metrics.y_scale)
			* BOLD_SCALE[dstWeightIndex] * BOLD_SCALE_Y;

	FT_Outline_EmboldenXY(&face->glyph->outline, extraX, extraY);

	if ((face->face_flags & FT_FACE_FLAG_FIXED_WIDTH) != 0) {
		FT_Outline_Translate(&outline, static_cast<int>(extraX / -2.f), 0);
	}
}

static void apply_synthetic_italic(FT_Face face, FT_Outline& outline, FontStyle /*srcStyle*/,
		FontStyle dstStyle) {
	auto shearAngle = dstStyle == FontStyle::ITALIC ? ITALIC_SHEAR : -ITALIC_SHEAR;

	FT_Matrix shearMatrix{
		.xx = 1L << 16,
		.xy = static_cast<FT_Long>(std::scalbn(std::sin(shearAngle), 16)),
		.yx = 0L,
		.yy = 1L << 16,
	};

	FT_Outline_Transform(&outline, &shearMatrix);
}

