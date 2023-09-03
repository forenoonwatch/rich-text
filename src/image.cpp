#include "image.hpp"

#include <glad/glad.h>

Image::Image(unsigned internalFormat, unsigned format, unsigned width, unsigned height, unsigned type,
		void* data) {
	glGenTextures(1, &m_handle);
	bind();
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, format, type, data);
}

Image::~Image() {
	if (m_handle) {
		glDeleteTextures(1, &m_handle);
	}
}

Image::Image(Image&& other) noexcept
		: m_handle(other.m_handle) {
	other.m_handle = 0;
}

Image& Image::operator=(Image&& other) noexcept {
	auto tmp = m_handle;
	m_handle = other.m_handle;
	other.m_handle = tmp;

	return *this;
}

void Image::bind(unsigned unit) const {
	glActiveTexture(GL_TEXTURE0 + unit);
	glBindTexture(GL_TEXTURE_2D, m_handle);
}

unsigned Image::get_handle() const {
	return m_handle;
}

