#include "bitmap.hpp"

#include <algorithm>
#include <cstring>

using namespace Text;

Bitmap::Bitmap(uint32_t width, uint32_t height)
		: m_data(std::make_unique<uint32_t[]>(width * height))
		, m_width(width)
		, m_height(height) {}

Bitmap::Bitmap(uint32_t width, uint32_t height, const Color& color)
		: m_data(std::make_unique_for_overwrite<uint32_t[]>(width * height))
		, m_width(width)
		, m_height(height) {
	clear(color);
}

void Bitmap::clear(const Color& color) {
	auto value = Color::to_argb(color);
	std::fill_n(m_data.get(), m_width * m_height, value);
}

void Bitmap::fill_rect(int32_t x, int32_t y, uint32_t width, uint32_t height, const Color& color) {
	x = std::max(0, x);
	y = std::max(0, y);

	auto value = Color::to_argb(color);
	auto* pPixels = &m_data[y * m_width + x];

	width = std::min(width, m_width - x);

	for (uint32_t i = y, l = std::min(y + height, m_height); i < l; ++i) {
		std::fill_n(pPixels, width, value);
		pPixels += m_width;
	}
}

void Bitmap::blit(const Bitmap& src, int32_t x, int32_t y) {
	x = std::max(0, x);
	y = std::max(0, y);

	auto width = std::min(src.get_width(), m_width - x);
	auto* pDstPixels = &m_data[y * m_width + x];
	auto* pSrcPixels = src.data();

	for (uint32_t i = y, l = std::min(y + src.get_height(), m_height); i < l; ++i) {
		std::memcpy(pDstPixels, pSrcPixels, width * sizeof(uint32_t));
		pSrcPixels += src.get_width();
		pDstPixels += m_width;
	}
}

void Bitmap::blit_alpha(const Bitmap& src, int32_t x, int32_t y, const Color& color) {
	auto startX = std::max(0, x);
	auto startY = std::max(0, y);
	auto endX = std::min(x + static_cast<int32_t>(src.get_width()), static_cast<int32_t>(m_width));
	auto endY = std::min(y + static_cast<int32_t>(src.get_height()), static_cast<int32_t>(m_height));

	for (int32_t iy = startY; iy < endY; ++iy) {
		for (int32_t ix = startX; ix < endX; ++ix) {
			auto srcColor = src.get_pixel(ix - x, iy - y) * color;
			auto dstColor = get_pixel(ix, iy);

			if (dstColor.a > 0.f) {
				set_pixel(ix, iy, Color::blend(srcColor, dstColor));
			}
			else {
				set_pixel(ix, iy, srcColor);
			}
		}
	}
}

void Bitmap::set_pixel(uint32_t x, uint32_t y, const Color& color) {
	m_data[y * m_width + x] = Color::to_argb(color);
}

Color Bitmap::get_pixel(uint32_t x, uint32_t y) const {
	return Color::from_argb_uint(m_data[y * m_width + x]);
}

uint32_t Bitmap::get_width() const {
	return m_width;
}

uint32_t Bitmap::get_height() const {
	return m_height;
}

uint32_t* Bitmap::data() {
	return m_data.get();
}

const uint32_t* Bitmap::data() const {
	return m_data.get();
}

