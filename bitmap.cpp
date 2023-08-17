#include "bitmap.hpp"

#include <algorithm>
#include <cstring>

constexpr uint32_t make_argb(float r, float g, float b, float a) {
	return ((static_cast<uint32_t>(r * 255.f) & 0xFFu) << 16)
			| ((static_cast<uint32_t>(g * 255.f) & 0xFFu) << 8)
			| ((static_cast<uint32_t>(b * 255.f) & 0xFFu) << 0)
			| ((static_cast<uint32_t>(a * 255.f) & 0xFFu) << 24);
}

constexpr uint32_t make_argb(const Color& color) {
	return make_argb(color.r, color.g, color.b, color.a);
}

constexpr Color make_color(uint32_t argb) {
	return {static_cast<float>((argb >> 16) & 0xFFu) / 255.f, static_cast<float>((argb >> 8) & 0xFFu) / 255.f,
			static_cast<float>(argb & 0xFFu) / 255.f, static_cast<float>((argb >> 24) & 0xFFu) / 255.f};
}

Bitmap::Bitmap(uint32_t width, uint32_t height)
		: m_data(std::make_unique<uint32_t[]>(width * height))
		, m_width(width)
		, m_height(height) {}

void Bitmap::clear(const Color& color) {
	auto value = make_argb(color);
	std::fill_n(m_data.get(), m_width * m_height, value);
}

void Bitmap::fill_rect(int32_t x, int32_t y, uint32_t width, uint32_t height, const Color& color) {
	x = std::max(0, x);
	y = std::max(0, y);

	auto value = make_argb(color);
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
		std::memcpy(pDstPixels, pSrcPixels, width);
		pSrcPixels += src.get_width();
		pDstPixels += m_width;
	}
}

void Bitmap::blit_alpha(const Bitmap& src, int32_t x, int32_t y, const Color& color) {
	x = std::max(0, x);
	y = std::max(0, y);

	auto width = std::min(src.get_width(), m_width - x);

	for (uint32_t iy = y, ly = std::min(y + src.get_height(), m_height); iy < ly; ++iy) {
		for (uint32_t ix = x, lx = x + width; ix < lx; ++ix) {
			auto srcColor = src.get_pixel(ix - x, iy - y) * color;
			auto dstColor = get_pixel(ix, iy);

			set_pixel(ix, iy, {
				.r = srcColor.r * srcColor.a + dstColor.r * (1.f - srcColor.a),
				.g = srcColor.g * srcColor.a + dstColor.g * (1.f - srcColor.a),
				.b = srcColor.b * srcColor.a + dstColor.b * (1.f - srcColor.a),
				.a = dstColor.a,
			});
		}
	}
}

void Bitmap::set_pixel(uint32_t x, uint32_t y, const Color& color) {
	m_data[y * m_width + x] = make_argb(color);
}

Color Bitmap::get_pixel(uint32_t x, uint32_t y) const {
	return make_color(m_data[y * m_width + x]);
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

