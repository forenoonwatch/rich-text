#include "font.hpp"

#include "font_cache.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_TRUETYPE_TABLES_H
#include FT_STROKER_H

#include <hb.h>

#include <layout/LEScripts.h>

#include <msdfgen.h>

static constexpr bool USE_MSDF_ERROR_CORRECTION = false;
static constexpr uint32_t MSDF_PADDING = 2;

namespace {

struct OutlineContext {
	msdfgen::Point2 position;
	msdfgen::Shape* pShape;
	msdfgen::Contour* pContour;
};

}

static int msdf_move_to(const FT_Vector* to, void* userData);
static int msdf_line_to(const FT_Vector* to, void* userData);
static int msdf_conic_to(const FT_Vector* control, const FT_Vector* to, void* userData);
static int msdf_cubic_to(const FT_Vector* control1, const FT_Vector* control2, const FT_Vector* to,
		void* userData);

static Bitmap load_msdf_shape(msdfgen::Shape& shape, FT_Outline& outline, float scaleX, float scaleY);

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

FontGlyphResult Font::get_outline_glyph(uint32_t glyphIndex, uint8_t thickness, StrokeType strokeType,
		float* offsetOut) const {
	FT_Load_Glyph(m_ftFace, glyphIndex, FT_LOAD_NO_BITMAP);

	FT_Glyph glyph;
	FT_Get_Glyph(m_ftFace->glyph, &glyph);
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

Bitmap Font::get_msdf_glyph(uint32_t glyphIndex, float* offsetOut) const {
	//FT_Load_Glyph(m_ftFace, glyphIndex, FT_LOAD_NO_SCALE);
	FT_Load_Glyph(m_ftFace, glyphIndex, FT_LOAD_BITMAP_METRICS_ONLY);
	msdfgen::Shape shape{};
	offsetOut[0] = static_cast<float>(m_ftFace->glyph->bitmap_left);
	offsetOut[1] = static_cast<float>(-m_ftFace->glyph->bitmap_top);

	return load_msdf_shape(shape, m_ftFace->glyph->outline, 1.f, 1.f);
}

Bitmap Font::get_msdf_outline_glyph(uint32_t glyphIndex, uint8_t thickness, StrokeType type,
		float* offsetOut) const {
	//FT_Load_Glyph(m_ftFace, glyphIndex, FT_LOAD_NO_BITMAP | FT_LOAD_NO_SCALE);
	FT_Load_Glyph(m_ftFace, glyphIndex, FT_LOAD_BITMAP_METRICS_ONLY);

	FT_Glyph glyph;
	FT_Get_Glyph(m_ftFace->glyph, &glyph);
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

hb_font_t* Font::get_hb_font() const {
	return m_hbFont;
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

// Static Functions

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

