#include <cstdio>

#include "file_read_bytes.hpp"
#include "font_cache.hpp"
#include "text_box.hpp"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "image.hpp"
#include "pipeline.hpp"
#include "text_atlas.hpp"
#include "msdf_text_atlas.hpp"


static TextBox* g_textBox = nullptr;

static int g_width = 640;
static int g_height = 480;

static void on_key_event(GLFWwindow* window, int key, int scancode, int action, int mods);
static void on_mouse_button_event(GLFWwindow* window, int button, int action, int mods);
static void on_text_event(GLFWwindow* window, unsigned codepoint);
static void on_focus_event(GLFWwindow* window, int focused);
static void on_resize(GLFWwindow* window, int width, int height);

static void render();

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

	glfwInit();
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	auto* window = glfwCreateWindow(g_width, g_height, "Font Tests", nullptr, nullptr);

	glfwMakeContextCurrent(window);
	gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress));

	glEnable(GL_DEBUG_OUTPUT);
	glDebugMessageCallback(gl_message_callback, 0);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	init_pipelines();

	g_textAtlas = new TextAtlas;
	g_msdfTextAtlas = new MSDFTextAtlas;

	g_textBox = new TextBox;
	g_textBox->set_rich_text(true);

	{
		std::vector<char> fileData;
		if (fileData = file_read_bytes("Sample.txt"); !fileData.empty()) {
			std::string str(fileData.data(), fileData.size());
			g_textBox->set_text(std::move(str));
		}
		else {
			g_textBox->set_text("Error: Sample.txt must be present in the build directory");
		}
	}

	g_textBox->set_font(std::move(font));

	on_resize(window, g_width, g_height);

	glfwSetKeyCallback(window, on_key_event);
	glfwSetMouseButtonCallback(window, on_mouse_button_event);
	glfwSetCharCallback(window, on_text_event);
	glfwSetWindowFocusCallback(window, on_focus_event);
	glfwSetFramebufferSizeCallback(window, on_resize);

	while (!glfwWindowShouldClose(window)) {
		render();
		glfwSwapBuffers(window);
		glfwPollEvents();
	}

	delete g_textBox;

	delete g_msdfTextAtlas;
	delete g_textAtlas;

	deinit_pipelines();

	glfwTerminate();
}

static void on_key_event(GLFWwindow* window, int key, int scancode, int action, int mods) {
	if (key == GLFW_KEY_ESCAPE) {
		glfwSetWindowShouldClose(window, true);
	}
	else {
		g_textBox->handle_key_press(key, action, mods);
	}
}

static void on_mouse_button_event(GLFWwindow* window, int button, int action, int mods) {
	double mouseX, mouseY;
	glfwGetCursorPos(window, &mouseX, &mouseY);

	g_textBox->handle_mouse_button(button, action, mods, mouseX, mouseY);
}

static void on_text_event(GLFWwindow* window, unsigned codepoint) {
	g_textBox->handle_text_input(codepoint);
}

static void on_focus_event(GLFWwindow* window, int focused) {
	if (!focused) {
		if (auto* pFocusedTextBox = TextBox::get_focused_text_box()) {
			pFocusedTextBox->release_focus();
		}
	}
}

static void on_resize(GLFWwindow* window, int width, int height) {
	glViewport(0, 0, width, height);
	g_width = width;
	g_height = height;

	g_textBox->set_size(static_cast<float>(width), static_cast<float>(height));
}

static void render() {
	float invScreenSize[] = {1.f / static_cast<float>(g_width), 1.f / static_cast<float>(g_height)};

	glClearColor(1.f, 1.f, 1.f, 1.f);
	glClear(GL_COLOR_BUFFER_BIT);

	g_textBox->render(invScreenSize);

}
