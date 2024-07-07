#include "msdf_text_atlas.hpp"

#include "font_registry.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <glad/glad.h>

#include <cstring>

static constexpr const size_t HASH_BASE = 0xCBF29CE484222325ull;
static constexpr const size_t HASH_MULTIPLIER = 0x100000001B3ull;

static constexpr uint32_t TEXTURE_EXTENT = 2048u;
static constexpr uint32_t TEXTURE_PADDING = 2u;

// MSDFTextAtlas

Image* MSDFTextAtlas::get_glyph_info(Text::SingleScriptFont font, uint32_t glyphIndex,
		float* texCoordExtentsOut, float* sizeOut, float* offsetOut, bool& hasColorOut) {
	GlyphKey key{glyphIndex, font.face.handle};
	auto fontData = Text::FontRegistry::get_font_data(font);
	auto& metrics = fontData.ftFace->size->metrics;
	auto scaleX = static_cast<float>(metrics.x_ppem) / static_cast<float>(Text::MSDF_PIXELS_PER_EM);
	auto scaleY = static_cast<float>(metrics.y_ppem) / static_cast<float>(Text::MSDF_PIXELS_PER_EM);

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
	auto bitmap = fontData.get_msdf_glyph(glyphIndex, info.offset);
	bool hasColor = false;
	info.bitmapSize[0] = static_cast<float>(bitmap.get_width());
	info.bitmapSize[1] = static_cast<float>(bitmap.get_height());

	if (bitmap.get_width() > 0 && bitmap.get_height() > 0) {
		info.pPage = upload_glyph(bitmap, info.texCoordExtents, hasColor);
	}

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
	auto bitmap = fontData.get_msdf_outline_glyph(glyphIndex, thickness, type, info.offset);
	bool hasColor = false;
	info.bitmapSize[0] = static_cast<float>(bitmap.get_width());
	info.bitmapSize[1] = static_cast<float>(bitmap.get_height());

	if (bitmap.get_width() > 0 && bitmap.get_height() > 0) {
		info.pPage = upload_glyph(bitmap, info.texCoordExtents, hasColor);
	}

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

