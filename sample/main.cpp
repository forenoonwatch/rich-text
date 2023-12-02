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
#include "ui_container.hpp"

static int g_width = 640;
static int g_height = 480;

static constexpr const float INSET = 10.f;

static void on_key_event(GLFWwindow* window, int key, int scancode, int action, int mods);
static void on_mouse_button_event(GLFWwindow* window, int button, int action, int mods);
static void on_mouse_move_event(GLFWwindow* window, double xPos, double yPos);
static void on_text_event(GLFWwindow* window, unsigned codepoint);
static void on_focus_event(GLFWwindow* window, int focused);
static void on_resize(GLFWwindow* window, int width, int height);

static void render(UIContainer& container);

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
	auto font = fontCache.get_font(famIdx, FontWeightIndex::REGULAR, FontFaceStyle::NORMAL, 48);

	auto container = UIContainer::create();

	glfwInit();
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	auto* window = glfwCreateWindow(g_width, g_height, "Font Tests", nullptr, nullptr);
	glfwSetWindowUserPointer(window, container.get());

	glfwMakeContextCurrent(window);
	gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress));

	glEnable(GL_DEBUG_OUTPUT);
	glDebugMessageCallback(gl_message_callback, 0);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	init_pipelines();

	g_textAtlas = new TextAtlas;
	g_msdfTextAtlas = new MSDFTextAtlas;

	auto textBox = TextBox::create();
	textBox->set_position(INSET, 0.f);
	textBox->set_rich_text(true);
	textBox->set_font(std::move(font));
	textBox->set_size(g_width - 2 * INSET, g_height - INSET);
	textBox->set_parent(container.get());

	{
		std::vector<char> fileData;
		if (fileData = file_read_bytes("Sample.txt"); !fileData.empty()) {
			std::string str(fileData.data(), fileData.size());
			textBox->set_text(std::move(str));
		}
		else {
			textBox->set_text("Error: Sample.txt must be present in the build directory");
		}
	}

	on_resize(window, g_width, g_height);

	glfwSetKeyCallback(window, on_key_event);
	glfwSetMouseButtonCallback(window, on_mouse_button_event);
	glfwSetCursorPosCallback(window, on_mouse_move_event);
	glfwSetCharCallback(window, on_text_event);
	glfwSetWindowFocusCallback(window, on_focus_event);
	glfwSetFramebufferSizeCallback(window, on_resize);

	while (!glfwWindowShouldClose(window)) {
		render(*container);
		glfwSwapBuffers(window);
		glfwPollEvents();
	}

	delete g_msdfTextAtlas;
	delete g_textAtlas;

	deinit_pipelines();

	glfwTerminate();
}

static void on_key_event(GLFWwindow* window, int key, int scancode, int action, int mods) {
	auto* pContainer = reinterpret_cast<UIContainer*>(glfwGetWindowUserPointer(window));

	if (key == GLFW_KEY_ESCAPE) {
		glfwSetWindowShouldClose(window, true);
	}
	else {
		double mouseX, mouseY;
		glfwGetCursorPos(window, &mouseX, &mouseY);
		pContainer->handle_key_press(key, action, mods, mouseX, mouseY);
	}
}

static void on_mouse_button_event(GLFWwindow* window, int button, int action, int mods) {
	auto* pContainer = reinterpret_cast<UIContainer*>(glfwGetWindowUserPointer(window));

	double mouseX, mouseY;
	glfwGetCursorPos(window, &mouseX, &mouseY);

	pContainer->handle_mouse_button(button, action, mods, mouseX, mouseY);
}

static void on_mouse_move_event(GLFWwindow* window, double xPos, double yPos) {
	auto* pContainer = reinterpret_cast<UIContainer*>(glfwGetWindowUserPointer(window));
	pContainer->handle_mouse_move(xPos, yPos);
}

static void on_text_event(GLFWwindow* window, unsigned codepoint) {
	auto* pContainer = reinterpret_cast<UIContainer*>(glfwGetWindowUserPointer(window));
	pContainer->handle_text_input(codepoint);
}

static void on_focus_event(GLFWwindow* window, int focused) {
	auto* pContainer = reinterpret_cast<UIContainer*>(glfwGetWindowUserPointer(window));

	if (!focused) {
		pContainer->handle_focus_lost();
	}
}

static void on_resize(GLFWwindow* window, int width, int height) {
	auto* pContainer = reinterpret_cast<UIContainer*>(glfwGetWindowUserPointer(window));

	glViewport(0, 0, width, height);
	g_width = width;
	g_height = height;

	pContainer->set_size(static_cast<float>(width), static_cast<float>(height));

	pContainer->for_each_child([&](auto& child) {
		child.set_size(static_cast<float>(width) - 2 * INSET, static_cast<float>(height) - INSET);
		return IterationDecision::CONTINUE;
	});
}

static void render(UIContainer& container) {
	glClearColor(1.f, 1.f, 1.f, 1.f);
	glClear(GL_COLOR_BUFFER_BIT);
	container.render();
}

