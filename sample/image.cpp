#include "image.hpp"

#include <glad/glad.h>

Image::Image(unsigned internalFormat, unsigned format, unsigned width, unsigned height, unsigned type,
			const void* data)
		: m_width(width)
		, m_height(height)
		, m_internalFormat(internalFormat)
		, m_format(format)
		, m_type(type) {
	glGenTextures(1, &m_handle);
	glBindTexture(GL_TEXTURE_2D, m_handle);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, format, type, data);
}

Image::~Image() {
	if (m_handle) {
		glDeleteTextures(1, &m_handle);
	}
}

Image::Image(Image&& other) noexcept
		: m_handle(other.m_handle)
		, m_width(other.m_width)
		, m_height(other.m_height)
		, m_internalFormat(other.m_internalFormat)
		, m_format(other.m_format)
		, m_type(other.m_type) {
	other.m_handle = 0;
}

Image& Image::operator=(Image&& other) noexcept {
	auto tmp = m_handle;
	m_handle = other.m_handle;
	m_width = other.m_width;
	m_height = other.m_height;
	m_internalFormat = other.m_internalFormat;
	m_format = other.m_format;
	m_type = other.m_type;
	other.m_handle = tmp;

	return *this;
}

void Image::write(int x, int y, unsigned width, unsigned height, const void* data) {
	glTextureSubImage2D(m_handle, 0, x, y, width, height, m_format, m_type, data);
}

void Image::bind(unsigned unit) const {
	glActiveTexture(GL_TEXTURE0 + unit);
	glBindTexture(GL_TEXTURE_2D, m_handle);
}

Image::operator bool() const {
	return m_handle;
}

unsigned Image::get_handle() const {
	return m_handle;
}

