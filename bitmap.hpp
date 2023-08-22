#pragma once

#include <cstdint>

#include <memory>

struct Color {
	float r;
	float g;
	float b;
	float a;

	static constexpr Color from_rgb(float r, float g, float b, float a = 255.f) {
		return {r / 255.f, g / 255.f, b / 255.f, a / 255.f};
	}

	constexpr Color operator*(const Color& c) const {
		return {r * c.r, g * c.g, b * c.b, a * c.a};
	}
};

class Bitmap final {
	public:
		Bitmap() = default;
		explicit Bitmap(uint32_t width, uint32_t height);

		Bitmap(Bitmap&&) noexcept = default;
		Bitmap& operator=(Bitmap&&) noexcept = default;

		Bitmap(const Bitmap&) = delete;
		void operator=(const Bitmap&) = delete;

		void clear(const Color&);

		void fill_rect(int32_t x, int32_t y, uint32_t width, uint32_t height, const Color&);

		void blit(const Bitmap& src, int32_t x, int32_t y);
		void blit_alpha(const Bitmap& src, int32_t x, int32_t y, const Color& = {1.f, 1.f, 1.f, 1.f});

		void set_pixel(uint32_t x, uint32_t y, const Color&);

		Color get_pixel(uint32_t x, uint32_t y) const;

		uint32_t get_width() const;
		uint32_t get_height() const;

		uint32_t* data();
		const uint32_t* data() const;
	private:
		std::unique_ptr<uint32_t[]> m_data{};
		uint32_t m_width{};
		uint32_t m_height{};
};
