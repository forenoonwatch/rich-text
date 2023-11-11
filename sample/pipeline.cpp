#include "pipeline.hpp"

#include "image.hpp"

#include "rect_shader.hpp"
#include "msdf_shader.hpp"
#include "outline_shader.hpp"

#include <glad/glad.h>

#include <utility>

#include <cstdio>

static void try_compile_program(const char* vertexSource, const char* fragmentSource, unsigned& outProgram);
static bool try_compile_shader(const char* source, GLenum type, unsigned& outShader);

void init_pipelines() {
	g_pipelines[static_cast<size_t>(PipelineIndex::RECT)] = Pipeline(RectShader::vertexShader,
			RectShader::fragmentShader, GL_TRIANGLES, 6);
	g_pipelines[static_cast<size_t>(PipelineIndex::MSDF)] = Pipeline(MSDFShader::vertexShader,
			MSDFShader::fragmentShader, GL_TRIANGLES, 6);
	g_pipelines[static_cast<size_t>(PipelineIndex::OUTLINE)] = Pipeline(OutlineShader::vertexShader,
			OutlineShader::fragmentShader, GL_LINE_LOOP, 4);
}

void deinit_pipelines() {
	for (size_t i = 0; i < static_cast<size_t>(PipelineIndex::COUNT); ++i) {
		g_pipelines[i] = {};
	}
}

Pipeline::Pipeline(const char* vertexSource, const char* fragmentSource, unsigned primitive,
			unsigned vertexCount)
		: m_primitive(primitive)
		, m_vertexCount(vertexCount) {
	glCreateVertexArrays(1, &m_vao);
	try_compile_program(vertexSource, fragmentSource, m_program);
}

Pipeline::~Pipeline() {
	if (m_vao) {
		glDeleteVertexArrays(1, &m_vao);
	}

	if (m_program) {
		glDeleteProgram(m_program);
	}
}

Pipeline::Pipeline(Pipeline&& other) noexcept
		: m_vao(other.m_vao)
		, m_program(other.m_program)
		, m_primitive(other.m_primitive)
		, m_vertexCount(other.m_vertexCount) {
	other.m_vao = 0;
	other.m_program = 0;
}

Pipeline& Pipeline::operator=(Pipeline&& other) noexcept {
	std::swap(m_vao, other.m_vao);
	std::swap(m_program, other.m_program);
	m_primitive = other.m_primitive;
	m_vertexCount = other.m_vertexCount;

	return *this;
}

void Pipeline::set_uniform(unsigned uniform, int value) const {
	glUniform1i(uniform, value);
}

void Pipeline::set_uniform_float2(unsigned uniform, const float* value) const {
	glUniform2fv(uniform, 1, value);
}

void Pipeline::set_uniform_float4(unsigned uniform, const float* value) const {
	glUniform4fv(uniform, 1, value);
}

void Pipeline::bind() const {
	glBindVertexArray(m_vao);
	glUseProgram(m_program);
}

void Pipeline::draw() const {
	glDrawArrays(m_primitive, 0, m_vertexCount);
}

static void try_compile_program(const char* vertexSource, const char* fragmentSource, unsigned& outProgram) {
	outProgram = 0;

	if (!vertexSource) {
		return;
	}

	unsigned vertexShader{};
	unsigned fragmentShader{};

	if (!try_compile_shader(vertexSource, GL_VERTEX_SHADER, vertexShader)) {
		return;
	}

	if (fragmentSource && !try_compile_shader(fragmentSource, GL_FRAGMENT_SHADER, fragmentShader)) {
		glDeleteShader(vertexShader);
		return;
	}

	unsigned shaderProgram = glCreateProgram();

	glAttachShader(shaderProgram, vertexShader);

	if (fragmentShader) {
		glAttachShader(shaderProgram, fragmentShader);
	}

	glLinkProgram(shaderProgram);

	int success;
	glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);

	if (success) {
		outProgram = shaderProgram;
	}
	else {
		char infoLog[512]{};
		glGetProgramInfoLog(shaderProgram, 512, nullptr, infoLog);
		printf("[GL] %s\n", infoLog);

		glDeleteProgram(shaderProgram);
	}

	glDeleteShader(vertexShader);

	if (fragmentShader) {
		glDeleteShader(fragmentShader);
	}
}

static bool try_compile_shader(const char* source, GLenum type, unsigned& outShader) {
	outShader = 0;
	unsigned shader = glCreateShader(type);
	glShaderSource(shader, 1, &source, nullptr);
	glCompileShader(shader);

	int success{};
	glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
	if (!success) {
		char infoLog[512]{};
		glGetShaderInfoLog(shader, 512, nullptr, infoLog);
		printf("[GL] %s\n", infoLog);

		glDeleteShader(shader);
		return false;
	}

	outShader = shader;
	return true;
}

