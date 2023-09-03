#include <cstdio>

#include "file_read_bytes.hpp"
#include "font_cache.hpp"
#include "text_box.hpp"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "image.hpp"
#include "pipeline.hpp"

#include "fullscreen_triangle_shader.hpp"

static Bitmap g_bitmap;
static TextBox g_textBox;

static Pipeline g_fullscreenTriangle;

static void on_resize(GLFWwindow* window, int width, int height);

static void GLAPIENTRY gl_message_callback(GLenum source, GLenum type, GLuint id, GLenum severity,
		GLsizei length, const GLchar* message, const void* userParam) {
	if (type == GL_DEBUG_TYPE_ERROR) {
		fprintf(stderr, "GL CALLBACK: **ERROR** type = 0x%x, severity = 0x%x, message = %s\n", type, severity,
				message);
	}
}

int main() {
	FontCache fontCache("fonts/families");
	auto famIdx = fontCache.get_font_family("Noto Sans"); 
	auto font = fontCache.get_font(famIdx, FontWeightIndex::REGULAR, FontFaceStyle::NORMAL, 24);

	g_textBox.set_rich_text(true);

	{
		std::vector<char> fileData;
		if (fileData = file_read_bytes("Sample.txt"); fileData.empty()) {
			puts("File Sample.txt must be present!");
			return 1;
		}

		std::string str(fileData.data(), fileData.size());

		g_textBox.set_text(std::move(str));
	}

	g_textBox.set_font(std::move(font));

	glfwInit();
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	auto* window = glfwCreateWindow(640, 480, "Font Tests", nullptr, nullptr);

	glfwMakeContextCurrent(window);
	gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress));

	glEnable(GL_DEBUG_OUTPUT);
	glDebugMessageCallback(gl_message_callback, 0);

	g_fullscreenTriangle = Pipeline(FullscreenTriangle::vertexShader, FullscreenTriangle::fragmentShader);

	on_resize(window, 640, 480);

	Image image(GL_RGBA8, GL_BGRA, 640, 480, GL_UNSIGNED_BYTE, g_bitmap.data());

	glfwSetFramebufferSizeCallback(window, on_resize);

	while (!glfwWindowShouldClose(window)) {
		glClear(GL_COLOR_BUFFER_BIT);
		image.bind();
		g_fullscreenTriangle.bind();
		glDrawArrays(GL_TRIANGLES, 0, 3);
		glfwSwapBuffers(window);
		glfwPollEvents();
	}

	glfwTerminate();
}

static void on_resize(GLFWwindow* window, int width, int height) {
	glViewport(0, 0, width, height);

	g_textBox.set_size(static_cast<float>(width), static_cast<float>(height));

	g_bitmap = Bitmap(width, height);
	g_bitmap.clear({1.f, 1.f, 1.f, 1.f});

	g_textBox.render(g_bitmap);
}

