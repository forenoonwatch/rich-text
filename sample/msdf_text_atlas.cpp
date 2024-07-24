#include "msdf_text_atlas.hpp"

#include "font_registry.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H

#include <msdfgen.h>

#include <glad/glad.h>

#include <cstring>

static constexpr bool USE_MSDF_ERROR_CORRECTION = false;
static constexpr const double MSDF_PIXELS_PER_EM = 32.0;

static constexpr const size_t HASH_BASE = 0xCBF29CE484222325ull;
static constexpr const size_t HASH_MULTIPLIER = 0x100000001B3ull;

static constexpr uint32_t TEXTURE_EXTENT = 2048u;
static constexpr uint32_t TEXTURE_PADDING = 2u;

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

static Text::Bitmap load_msdf_shape(msdfgen::Shape& shape, FT_Outline& outline, float* offsetOut, int32_t upem);

// MSDFTextAtlas

Image* MSDFTextAtlas::get_glyph_info(Text::SingleScriptFont font, uint32_t glyphIndex,
		float* texCoordExtentsOut, float* sizeOut, float* offsetOut, bool& hasColorOut) {
	GlyphKey key{glyphIndex, font.face.handle};
	auto fontData = Text::FontRegistry::get_font_data(font);
	auto& metrics = fontData.ftFace->size->metrics;
	auto scaleX = static_cast<float>(metrics.x_ppem) / static_cast<float>(MSDF_PIXELS_PER_EM);
	auto scaleY = static_cast<float>(metrics.y_ppem) / static_cast<float>(MSDF_PIXELS_PER_EM);

	if (auto it = m_glyphs.find(key); it != m_glyphs.end()) {
		std::memcpy(texCoordExtentsOut, it->second.texCoordExtents, 4 * sizeof(float));
		std::memcpy(sizeOut, it->second.bitmapSize, 2 * sizeof(float));
		std::memcpy(offsetOut, it->second.offset, 2 * sizeof(float));
		offsetOut[0] *= scaleX;
		offsetOut[1] *= scaleY;
		sizeOut[0] *= scaleX;
		sizeOut[1] *= scaleY;
		hasColorOut = it->second.pPage ? it->second.pPage->hasColor : false;
		return &it->second.pPage->image;
	}

	GlyphInfo info{};
	bool hasColor = false;

	fontData.load_glyph_curve(glyphIndex, [&](auto& outline) {
		handle_rasterization(outline, info, hasColor, fontData.get_upem());
	});

	auto* result = info.pPage ? &info.pPage->image : nullptr;
	std::memcpy(texCoordExtentsOut, info.texCoordExtents, 4 * sizeof(float));
	std::memcpy(sizeOut, info.bitmapSize, 2 * sizeof(float));
	std::memcpy(offsetOut, info.offset, 2 * sizeof(float));
	hasColorOut = hasColor;

	offsetOut[0] *= scaleX;
	offsetOut[1] *= scaleY;
	sizeOut[0] *= scaleX;
	sizeOut[1] *= scaleY;

	m_glyphs.emplace(std::make_pair(std::move(key), std::move(info)));

	return result;
}

Image* MSDFTextAtlas::get_stroke_info(Text::SingleScriptFont font, uint32_t glyphIndex, uint8_t thickness,
		Text::StrokeType type, float* texCoordExtentsOut, float* sizeOut, float* offsetOut, bool& hasColorOut) {
	StrokeKey key{font.size, glyphIndex, font.face.handle, thickness, type};

	if (auto it = m_strokes.find(key); it != m_strokes.end()) {
		std::memcpy(texCoordExtentsOut, it->second.texCoordExtents, 4 * sizeof(float));
		std::memcpy(sizeOut, it->second.bitmapSize, 2 * sizeof(float));
		std::memcpy(offsetOut, it->second.offset, 2 * sizeof(float));
		hasColorOut = it->second.pPage ? it->second.pPage->hasColor : false;
		return &it->second.pPage->image;
	}

	GlyphInfo info{};
	auto fontData = Text::FontRegistry::get_font_data(font);
	bool hasColor = false;

	fontData.load_glyph_outline_curve(glyphIndex, thickness, type, [&](auto& outline) {
		handle_rasterization(outline, info, hasColor, fontData.get_upem());
	});

	auto* result = info.pPage ? &info.pPage->image : nullptr;
	std::memcpy(texCoordExtentsOut, info.texCoordExtents, 4 * sizeof(float));
	std::memcpy(sizeOut, info.bitmapSize, 2 * sizeof(float));
	std::memcpy(offsetOut, info.offset, 2 * sizeof(float));
	hasColorOut = hasColor;

	m_strokes.emplace(std::make_pair(std::move(key), std::move(info)));

	return result;
}

MSDFTextAtlas::Page* MSDFTextAtlas::upload_glyph(const Text::Bitmap& bitmap, float* texCoordExtentsOut,
		bool hasColor) {
	auto padWidth = bitmap.get_width() + TEXTURE_PADDING;
	auto padHeight = bitmap.get_height() + TEXTURE_PADDING;
	auto* pPage = get_or_create_target_page(padWidth, padHeight, hasColor);

	if (pPage->xOffset + padWidth > TEXTURE_EXTENT) {
		pPage->xOffset = 0;
		pPage->yOffset += pPage->lineHeight;
		pPage->lineHeight = padHeight;
	}

	pPage->image.write(static_cast<int>(pPage->xOffset), static_cast<int>(pPage->yOffset), bitmap.get_width(),
			bitmap.get_height(), bitmap.data());

	texCoordExtentsOut[0] = static_cast<float>(pPage->xOffset) / static_cast<float>(TEXTURE_EXTENT);
	texCoordExtentsOut[1] = static_cast<float>(pPage->yOffset) / static_cast<float>(TEXTURE_EXTENT);
	texCoordExtentsOut[2] = static_cast<float>(bitmap.get_width()) / static_cast<float>(TEXTURE_EXTENT);
	texCoordExtentsOut[3] = static_cast<float>(bitmap.get_height()) / static_cast<float>(TEXTURE_EXTENT);

	pPage->xOffset += padWidth;

	if (pPage->lineHeight < padHeight) {
		pPage->lineHeight = padHeight;
	}

	return pPage;
}

void MSDFTextAtlas::handle_rasterization(FT_Outline& outline, GlyphInfo& info, bool& hasColor, int32_t upem) {
	msdfgen::Shape shape{};
	auto bitmap = load_msdf_shape(shape, outline, info.offset, upem);
	info.bitmapSize[0] = static_cast<float>(bitmap.get_width());
	info.bitmapSize[1] = static_cast<float>(bitmap.get_height());
	hasColor = false;

	if (bitmap.get_width() > 0 && bitmap.get_height() > 0) {
		info.pPage = upload_glyph(bitmap, info.texCoordExtents, hasColor);
	}
}

MSDFTextAtlas::Page* MSDFTextAtlas::get_or_create_target_page(uint32_t width, uint32_t height, bool hasColor) {
	for (auto& pPage : m_pages) {
		if (pPage->hasColor == hasColor && page_can_fit_glyph(*pPage, width, height)) {
			return pPage.get();
		}
	}

	auto page = std::make_unique<Page>();
	page->image = Image(GL_RGBA8, GL_BGRA, TEXTURE_EXTENT, TEXTURE_EXTENT, GL_UNSIGNED_BYTE);
	page->xOffset = 0;
	page->yOffset = 0;
	page->lineHeight = 0;
	page->hasColor = hasColor;

	auto* pPage = page.get();
	m_pages.emplace_back(std::move(page));

	return pPage;
}

bool MSDFTextAtlas::page_can_fit_glyph(Page& page, uint32_t width, uint32_t height) {
	return (page.xOffset + width <= TEXTURE_EXTENT && page.yOffset + height <= TEXTURE_EXTENT)
		|| (width <= TEXTURE_EXTENT && page.yOffset + page.lineHeight + height <= TEXTURE_EXTENT);
}

// MSDFTextAtlas::GlyphKey

bool MSDFTextAtlas::GlyphKey::operator==(const GlyphKey& o) const {
	return glyphIndex == o.glyphIndex && face == o.face;
}

// MSDFTextAtlas::GlyphKeyHash

size_t MSDFTextAtlas::GlyphKeyHash::operator()(const GlyphKey& k) const {
	// FNV-11a
	size_t hash = HASH_BASE;
	hash = static_cast<size_t>(hash * HASH_MULTIPLIER) ^ static_cast<size_t>(k.glyphIndex);
	hash = static_cast<size_t>(hash * HASH_MULTIPLIER) ^ static_cast<size_t>(k.face);
	return hash;
}

// MSDFTextAtlas::StrokeKey

bool MSDFTextAtlas::StrokeKey::operator==(const StrokeKey& o) const {
	return glyphSize == o.glyphSize && glyphIndex == o.glyphIndex && face == o.face
			&& strokeSize == o.strokeSize && type == o.type;
}

// TextAtlas::StrokeKeyHash

size_t MSDFTextAtlas::StrokeKeyHash::operator()(const StrokeKey& k) const {
	// FNV-11a
	size_t hash = HASH_BASE;
	hash = static_cast<size_t>(hash * HASH_MULTIPLIER) ^ static_cast<size_t>(k.glyphSize);
	hash = static_cast<size_t>(hash * HASH_MULTIPLIER) ^ static_cast<size_t>(k.glyphIndex);
	hash = static_cast<size_t>(hash * HASH_MULTIPLIER) ^ static_cast<size_t>(k.face);
	hash = static_cast<size_t>(hash * HASH_MULTIPLIER) ^ static_cast<size_t>(k.strokeSize);
	hash = static_cast<size_t>(hash * HASH_MULTIPLIER) ^ static_cast<size_t>(k.type);
	return hash;
}

// Static Functions

static constexpr float saturate(float v) {
	return v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
}

static constexpr Text::Color msdf_make_color(const float* c) {
	return {saturate(c[0]), saturate(c[1]), saturate(c[2]), 1.f};
}

static Text::Bitmap load_msdf_shape(msdfgen::Shape& shape, FT_Outline& outline, float* offsetOut, 
		int32_t upem) {
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

	shape.normalize();
	auto bounds = shape.getBounds();

	auto scale = MSDF_PIXELS_PER_EM / static_cast<double>(upem);
	auto range = 2.0 * static_cast<double>(upem) / MSDF_PIXELS_PER_EM;

	auto l = bounds.l, b = bounds.b, r = bounds.r, t = bounds.t;

	l -= 0.5 * range;
	r += 0.5 * range;
	b -= 0.5 * range;
	t += 0.5 * range;

	auto w = scale * (r - l);
	auto h = scale * (t - b);

	auto width = static_cast<int32_t>(w + 1.0) + 1;
	auto height = static_cast<int32_t>(h + 1.0) + 1;

	auto tx = -l + 0.5 * (width - w) / scale;
	auto ty = -b + 0.5 * (height - h) / scale;

	offsetOut[0] = l * scale - 0.5 * (width - w);
	offsetOut[1] = -t * scale - 0.5 * (height - h);

	msdfgen::Projection projection{{scale, scale}, {tx, ty}};
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
	msdfgen::generateMSDF(bmp, shape, projection, range, generatorConfig);

	if constexpr (USE_MSDF_ERROR_CORRECTION) {
		msdfgen::distanceSignCorrection(bmp, shape, projection);
		msdfgen::msdfErrorCorrection(bmp, shape, projection, range, postErrorCorrectionConfig);
	}

	Text::Bitmap result(width, height);

	for (int32_t y = 0; y < height; ++y) {
		for (int32_t x = 0; x < width; ++x) {
			result.set_pixel(x, y, msdf_make_color(bmp(x, y)));
		}
	}

	return result;
}

static msdfgen::Point2 make_point2(const FT_Vector& v) {
	return {(double)v.x, (double)v.y};
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

