#pragma once

class Image final {
	public:
		Image() = default;
		explicit Image(unsigned internalFormat, unsigned format, unsigned width, unsigned height, unsigned type,
				const void* data = nullptr);
		~Image();

		Image(Image&&) noexcept;
		Image& operator=(Image&&) noexcept;

		Image(const Image&) = delete;
		void operator=(const Image&) = delete;

		void write(int x, int y, unsigned width, unsigned height, const void*);

		void bind(unsigned unit = 0) const;

		explicit operator bool() const;

		unsigned get_handle() const;
	private:
		unsigned m_handle{};
		unsigned m_width;
		unsigned m_height;
		unsigned m_internalFormat;
		unsigned m_format;
		unsigned m_type;
};

