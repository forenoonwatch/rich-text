#include "text_atlas.hpp"

#include "font_registry.hpp"

#include <glad/glad.h>

#include <cstring>

static constexpr const size_t HASH_BASE = 0xCBF29CE484222325ull;
static constexpr const size_t HASH_MULTIPLIER = 0x100000001B3ull;

static constexpr uint32_t TEXTURE_EXTENT = 2048u;
static constexpr uint32_t TEXTURE_PADDING = 1u;

// TextAtlas

TextAtlas::TextAtlas() {
	uint8_t imageData[8 * 8 * 4];
	std::memset(imageData, 0xFF, 8 * 8 * 4);
	m_defaultImage = Image(GL_RGBA8, GL_RGBA, 8, 8, GL_UNSIGNED_BYTE, imageData);
}

Image* TextAtlas::get_glyph_info(Text::SingleScriptFont font, uint32_t glyphIndex, float* texCoordExtentsOut,
		float* sizeOut, float* offsetOut, bool& hasColorOut) {
	GlyphKey key{font.get_effective_size(), glyphIndex, font.face.handle, font.weight, font.style};

	if (auto it = m_glyphs.find(key); it != m_glyphs.end()) {
		std::memcpy(texCoordExtentsOut, it->second.texCoordExtents, 4 * sizeof(float));
		std::memcpy(sizeOut, it->second.bitmapSize, 2 * sizeof(float));
		std::memcpy(offsetOut, it->second.offset, 2 * sizeof(float));
		hasColorOut = it->second.pPage ? it->second.pPage->hasColor : false;
		offsetOut[1] += font.get_baseline_offset();
		return &it->second.pPage->image;
	}

	GlyphInfo info{};
	auto fontData = Text::FontRegistry::get_font_data(font);
	bool hasColor = false;

	fontData.rasterize_glyph(glyphIndex, [&](const auto& rasterInfo) {
		handle_rasterization(rasterInfo, info, hasColor);
	});

	auto* result = info.pPage ? &info.pPage->image : nullptr;
	std::memcpy(texCoordExtentsOut, info.texCoordExtents, 4 * sizeof(float));
	std::memcpy(sizeOut, info.bitmapSize, 2 * sizeof(float));
	std::memcpy(offsetOut, info.offset, 2 * sizeof(float));
	hasColorOut = hasColor;

	m_glyphs.emplace(std::make_pair(std::move(key), std::move(info)));

	offsetOut[1] += font.get_baseline_offset();
	return result;
}

Image* TextAtlas::get_stroke_info(Text::SingleScriptFont font, uint32_t glyphIndex, uint8_t thickness,
		Text::StrokeType type, float* texCoordExtentsOut, float* sizeOut, float* offsetOut, bool& hasColorOut) {
	StrokeKey key{font.get_effective_size(), glyphIndex, font.face.handle, thickness, type};

	if (auto it = m_strokes.find(key); it != m_strokes.end()) {
		std::memcpy(texCoordExtentsOut, it->second.texCoordExtents, 4 * sizeof(float));
		std::memcpy(sizeOut, it->second.bitmapSize, 2 * sizeof(float));
		std::memcpy(offsetOut, it->second.offset, 2 * sizeof(float));
		hasColorOut = it->second.pPage ? it->second.pPage->hasColor : false;
		offsetOut[1] += font.get_baseline_offset();
		return &it->second.pPage->image;
	}

	GlyphInfo info{};
	bool hasColor = false;
	auto fontData = Text::FontRegistry::get_font_data(font);

	fontData.rasterize_glyph_outline(glyphIndex, thickness, type, [&](const auto& rasterInfo) {
		handle_rasterization(rasterInfo, info, hasColor);
	});

	auto* result = info.pPage ? &info.pPage->image : nullptr;
	std::memcpy(texCoordExtentsOut, info.texCoordExtents, 4 * sizeof(float));
	std::memcpy(sizeOut, info.bitmapSize, 2 * sizeof(float));
	std::memcpy(offsetOut, info.offset, 2 * sizeof(float));
	hasColorOut = hasColor;

	m_strokes.emplace(std::make_pair(std::move(key), std::move(info)));

	offsetOut[1] += font.get_baseline_offset();
	return result;
}

Image* TextAtlas::get_default_texture() {
	return &m_defaultImage;
}

void TextAtlas::handle_rasterization(const Text::FontRasterizeInfo& rasterInfo, GlyphInfo& info,
		bool& hasColor) {
	info.bitmapSize[0] = static_cast<float>(rasterInfo.width);
	info.bitmapSize[1] = static_cast<float>(rasterInfo.height);
	info.offset[0] = rasterInfo.offsetX;
	info.offset[1] = rasterInfo.offsetY;
	hasColor = rasterInfo.format == Text::FontRasterFormat::BGRA8;

	Text::Bitmap bitmap(rasterInfo.width, rasterInfo.height);

	if (rasterInfo.format == Text::FontRasterFormat::R8) {
		for (uint32_t y = 0, i = 0; y < rasterInfo.height; ++y) {
			for (uint32_t x = 0; x < rasterInfo.width; ++x, ++i) {
				auto alpha = static_cast<float>(rasterInfo.pData[i]) / 255.f;
				bitmap.set_pixel(x, y, {1.f, 1.f, 1.f, alpha});
			}
		}
	}
	else if (rasterInfo.format == Text::FontRasterFormat::BGRA8) {
		for (uint32_t y = 0, i = 0; y < rasterInfo.height; ++y) {
			for (uint32_t x = 0; x < rasterInfo.width; ++x, i += 4) {
				auto b = static_cast<float>(rasterInfo.pData[i]) / 255.f;
				auto g = static_cast<float>(rasterInfo.pData[i + 1]) / 255.f;
				auto r = static_cast<float>(rasterInfo.pData[i + 2]) / 255.f;
				auto a = static_cast<float>(rasterInfo.pData[i + 3]) / 255.f;
				bitmap.set_pixel(x, y, {r / a, g / a, b / a, a});
			}
		}
	}

	if (rasterInfo.width > 0 && rasterInfo.height > 0) {
		info.pPage = upload_glyph(bitmap, info.texCoordExtents, hasColor);
	}
}

TextAtlas::Page* TextAtlas::upload_glyph(const Text::Bitmap& bitmap, float* texCoordExtentsOut, bool hasColor) {
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

TextAtlas::Page* TextAtlas::get_or_create_target_page(uint32_t width, uint32_t height, bool hasColor) {
	for (auto& pPage : m_pages) {
		if (pPage->hasColor == hasColor && page_can_fit_glyph(*pPage, width, height)) {
			return pPage.get();
		}
	}

	auto page = std::make_unique<Page>();
	page->image = Image(GL_RGBA8, GL_RGBA, TEXTURE_EXTENT, TEXTURE_EXTENT, GL_UNSIGNED_BYTE);
	page->xOffset = 0;
	page->yOffset = 0;
	page->lineHeight = 0;
	page->hasColor = hasColor;

	auto* pPage = page.get();
	m_pages.emplace_back(std::move(page));

	return pPage;
}

bool TextAtlas::page_can_fit_glyph(Page& page, uint32_t width, uint32_t height) {
	return (page.xOffset + width <= TEXTURE_EXTENT && page.yOffset + height <= TEXTURE_EXTENT)
		|| (width <= TEXTURE_EXTENT && page.yOffset + page.lineHeight + height <= TEXTURE_EXTENT);
}

// TextAtlas::GlyphKey

bool TextAtlas::GlyphKey::operator==(const GlyphKey& o) const {
	return size == o.size && glyphIndex == o.glyphIndex && face == o.face && weight == o.weight
			&& style == o.style;
}

// TextAtlas::GlyphKeyHash

size_t TextAtlas::GlyphKeyHash::operator()(const GlyphKey& k) const {
	// FNV-11a
	size_t hash = HASH_BASE;
	hash = static_cast<size_t>(hash * HASH_MULTIPLIER) ^ static_cast<size_t>(k.size);
	hash = static_cast<size_t>(hash * HASH_MULTIPLIER) ^ static_cast<size_t>(k.glyphIndex);
	hash = static_cast<size_t>(hash * HASH_MULTIPLIER) ^ static_cast<size_t>(k.face);
	hash = static_cast<size_t>(hash * HASH_MULTIPLIER) ^ static_cast<size_t>(k.weight);
	hash = static_cast<size_t>(hash * HASH_MULTIPLIER) ^ static_cast<size_t>(k.style);
	return hash;
}

// TextAtlas::StrokeKey

bool TextAtlas::StrokeKey::operator==(const StrokeKey& o) const {
	return glyphSize == o.glyphSize && glyphIndex == o.glyphIndex && face == o.face
			&& strokeSize == o.strokeSize && type == o.type;
}

// TextAtlas::StrokeKeyHash

size_t TextAtlas::StrokeKeyHash::operator()(const StrokeKey& k) const {
	// FNV-11a
	size_t hash = HASH_BASE;
	hash = static_cast<size_t>(hash * HASH_MULTIPLIER) ^ static_cast<size_t>(k.glyphSize);
	hash = static_cast<size_t>(hash * HASH_MULTIPLIER) ^ static_cast<size_t>(k.glyphIndex);
	hash = static_cast<size_t>(hash * HASH_MULTIPLIER) ^ static_cast<size_t>(k.face);
	hash = static_cast<size_t>(hash * HASH_MULTIPLIER) ^ static_cast<size_t>(k.strokeSize);
	hash = static_cast<size_t>(hash * HASH_MULTIPLIER) ^ static_cast<size_t>(k.type);
	return hash;
}

