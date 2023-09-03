#pragma once

class Image final {
	public:
		Image() = default;
		explicit Image(unsigned internalFormat, unsigned format, unsigned width, unsigned height, unsigned type,
				void* data = nullptr);
		~Image();

		Image(Image&&) noexcept;
		Image& operator=(Image&&) noexcept;

		Image(const Image&) = delete;
		void operator=(const Image&) = delete;

		void bind(unsigned unit = 0) const;

		unsigned get_handle() const;
	private:
		unsigned m_handle{};
};

