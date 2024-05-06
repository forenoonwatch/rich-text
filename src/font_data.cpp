#include "font_data.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_STROKER_H

#include <hb.h>

#include <msdfgen.h>

#include <cmath>

using namespace Text;

static constexpr bool USE_MSDF_ERROR_CORRECTION = false;
static constexpr uint32_t MSDF_PADDING = 2;

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

namespace {

struct OutlineContext {
	msdfgen::Point2 position;
	msdfgen::Shape* pShape;
	msdfgen::Contour* pContour;
};

}

static void try_apply_synthetics(FT_Face face, FT_Outline& outline, SyntheticFontInfo synthInfo);

static void apply_synthetic_bold(FT_Face face, FT_Outline& outline, FontWeight srcWeight, FontWeight dstWeight);
static void apply_synthetic_italic(FT_Face face, FT_Outline& outline, FontStyle srcStyle, FontStyle dstStyle);

static int msdf_move_to(const FT_Vector* to, void* userData);
static int msdf_line_to(const FT_Vector* to, void* userData);
static int msdf_conic_to(const FT_Vector* control, const FT_Vector* to, void* userData);
static int msdf_cubic_to(const FT_Vector* control1, const FT_Vector* control2, const FT_Vector* to,
		void* userData);

static Bitmap load_msdf_shape(msdfgen::Shape& shape, FT_Outline& outline, float scaleX, float scaleY);

float FontData::get_ascent() const {
	return static_cast<float>(ftFace->size->metrics.ascender) / 64.f
			/ calc_font_scale_modifier(synthInfo.syntheticSmallCaps,
					synthInfo.syntheticSubscript || synthInfo.syntheticSuperscript);
}

float FontData::get_descent() const {
	return static_cast<float>(ftFace->size->metrics.descender) / 64.f
			/ calc_font_scale_modifier(synthInfo.syntheticSmallCaps,
					synthInfo.syntheticSubscript || synthInfo.syntheticSuperscript);
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

FontGlyphResult FontData::rasterize_glyph(uint32_t glyph, float* offsetOut) const {
	FT_Load_Glyph(ftFace, glyph, FT_LOAD_NO_BITMAP | FT_LOAD_COLOR);

	try_apply_synthetics(ftFace, ftFace->glyph->outline, synthInfo);

	FT_Render_Glyph(ftFace->glyph, FT_RENDER_MODE_NORMAL);

	auto uWidth = static_cast<uint32_t>(ftFace->glyph->bitmap.width);
	auto uHeight = static_cast<uint32_t>(ftFace->glyph->bitmap.rows);

	FontGlyphResult result{
		.bitmap = Bitmap{uWidth, uHeight},
		.hasColor = false,
	};
	auto* buffer = ftFace->glyph->bitmap.buffer;

	switch (ftFace->glyph->bitmap.pixel_mode) {
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

	offsetOut[0] = static_cast<float>(ftFace->glyph->bitmap_left);
	offsetOut[1] = static_cast<float>(-ftFace->glyph->bitmap_top);

	return result;
}

FontGlyphResult FontData::rasterize_glyph_outline(uint32_t glyphIndex, uint8_t thickness, StrokeType strokeType,
		float* offsetOut) const {
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

	FontGlyphResult result{
		.bitmap = Bitmap{uWidth, uHeight},
		.hasColor = false,
	};

	for (uint32_t y = 0, i = 0; y < uHeight; ++y) {
		for (uint32_t x = 0; x < uWidth; ++x, ++i) {
			auto alpha = static_cast<float>(buffer[i]) / 255.f;
			result.bitmap.set_pixel(x, y, {1.f, 1.f, 1.f, alpha});
		}
	}

	offsetOut[0] = static_cast<float>(bmpGlyph->left);
	offsetOut[1] = static_cast<float>(-bmpGlyph->top);

	FT_Stroker_Done(stroker);
	FT_Done_Glyph(glyph);

	return result;
}

Bitmap FontData::get_msdf_glyph(uint32_t glyphIndex, float* offsetOut) const {
	//FT_Load_Glyph(ftFace, glyphIndex, FT_LOAD_NO_SCALE);
	FT_Load_Glyph(ftFace, glyphIndex, FT_LOAD_BITMAP_METRICS_ONLY);
	msdfgen::Shape shape{};
	offsetOut[0] = static_cast<float>(ftFace->glyph->bitmap_left);
	offsetOut[1] = static_cast<float>(-ftFace->glyph->bitmap_top);

	return load_msdf_shape(shape, ftFace->glyph->outline, 1.f, 1.f);
}

Bitmap FontData::get_msdf_outline_glyph(uint32_t glyphIndex, uint8_t thickness, StrokeType type,
		float* offsetOut) const {
	//FT_Load_Glyph(m_ftFace, glyphIndex, FT_LOAD_NO_BITMAP | FT_LOAD_NO_SCALE);
	FT_Load_Glyph(ftFace, glyphIndex, FT_LOAD_BITMAP_METRICS_ONLY);

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

	FT_Outline outline{};
	FT_Outline_New(glyph->library, points, contours, &outline);
	outline.n_points = 0;
	outline.n_contours = 0;

	FT_Stroker_Export(stroker, &outline);

	msdfgen::Shape shape{};
	auto result = load_msdf_shape(shape, outline, 1.f, 1.f);

	FT_Outline_Done(glyph->library, &outline);
	FT_Stroker_Done(stroker);
	FT_Done_Glyph(glyph);

	return result;
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

static constexpr float saturate(float v) {
	return v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
}

static constexpr Color msdf_make_color(const float* c) {
	return {saturate(c[0]), saturate(c[1]), saturate(c[2]), 1.f};
}

static Bitmap load_msdf_shape(msdfgen::Shape& shape, FT_Outline& outline, float scaleX, float scaleY) {
	shape.contours.clear();
	shape.inverseYAxis = true;

	FT_Outline_Funcs funcs{
		.move_to = msdf_move_to,
		.line_to = msdf_line_to,
		.conic_to = msdf_conic_to,
		.cubic_to = msdf_cubic_to,
	};

	OutlineContext ctx{
		.pShape = &shape,
	};

	FT_Outline_Decompose(&outline, &funcs, &ctx);

	if (!shape.contours.empty() && shape.contours.back().edges.empty()) {
		shape.contours.pop_back();
	}

	if (shape.edgeCount() == 0) {
		return {};
	}

	auto bounds = shape.getBounds();
	auto width = static_cast<int32_t>((bounds.r - bounds.l + 2.0 * static_cast<double>(MSDF_PADDING))
			* scaleX);
	auto height = static_cast<int32_t>((bounds.t - bounds.b + 2.0 * static_cast<double>(MSDF_PADDING))
			* scaleY);

	msdfgen::Vector2 scale{scaleX, scaleY};
	msdfgen::Vector2 translate{static_cast<double>(MSDF_PADDING)};
	msdfgen::Projection projection{scale, translate};
	msdfgen::Bitmap<float, 3> bmp(width, height);

	msdfgen::MSDFGeneratorConfig generatorConfig{};
	generatorConfig.overlapSupport = true;
	msdfgen::MSDFGeneratorConfig postErrorCorrectionConfig{generatorConfig};

	if constexpr (USE_MSDF_ERROR_CORRECTION) {
		generatorConfig.errorCorrection.mode = msdfgen::ErrorCorrectionConfig::DISABLED;
		postErrorCorrectionConfig.errorCorrection.distanceCheckMode
				= msdfgen::ErrorCorrectionConfig::DO_NOT_CHECK_DISTANCE;
	}

	//msdfgen::edgeColoringSimple(shape, 3.0, 0);
	msdfgen::edgeColoringInkTrap(shape, 3.0, 0);
	msdfgen::generateMSDF(bmp, shape, projection, 2.0, generatorConfig);

	if constexpr (USE_MSDF_ERROR_CORRECTION) {
		msdfgen::distanceSignCorrection(bmp, shape, projection);
		msdfgen::msdfErrorCorrection(bmp, shape, projection, 2.0, postErrorCorrectionConfig);
	}

	Bitmap result(width, height);

	for (int32_t y = 0; y < height; ++y) {
		for (int32_t x = 0; x < width; ++x) {
			result.set_pixel(x, y, msdf_make_color(bmp(x, y)));
		}
	}

	return result;
}

static msdfgen::Point2 make_point2(const FT_Vector& v) {
	return {scalbn(v.x, -6), scalbn(v.y, -6)};
}

static int msdf_move_to(const FT_Vector* to, void* userData) {
	auto& ctx = *reinterpret_cast<OutlineContext*>(userData);

	if (!ctx.pContour || ctx.pContour->edges.empty()) {
		ctx.pContour = &ctx.pShape->addContour();
	}

	ctx.position = make_point2(*to);

	return 0;
}

static int msdf_line_to(const FT_Vector* to, void* userData) {
	auto& ctx = *reinterpret_cast<OutlineContext*>(userData);

	if (auto endpoint = make_point2(*to); endpoint != ctx.position) {
		ctx.pContour->addEdge(new msdfgen::LinearSegment(ctx.position, endpoint));
		ctx.position = endpoint;
	}

	return 0;
}

static int msdf_conic_to(const FT_Vector* control, const FT_Vector* to, void* userData) {
	auto& ctx = *reinterpret_cast<OutlineContext*>(userData);
	ctx.pContour->addEdge(new msdfgen::QuadraticSegment(ctx.position, make_point2(*control),
			make_point2(*to)));
	ctx.position = make_point2(*to);
	return 0;
}

static int msdf_cubic_to(const FT_Vector* control1, const FT_Vector* control2, const FT_Vector* to,
		void* userData) {
	auto& ctx = *reinterpret_cast<OutlineContext*>(userData);
	ctx.pContour->addEdge(new msdfgen::CubicSegment(ctx.position, make_point2(*control1),
			make_point2(*control2), make_point2(*to)));
	ctx.position = make_point2(*to);
	return 0;
}

