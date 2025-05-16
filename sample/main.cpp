#include <cstdio>

#include "file_read_bytes.hpp"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "pipeline.hpp"
#include "text_atlas.hpp"
#include "msdf_text_atlas.hpp"

#include "text_box.hpp"
#include "tool_bar.hpp"
#include "tool_bar_menu.hpp"
#include "tool_bar_menu_item.hpp"
#include "ui_container.hpp"

#include "font_registry.hpp"

#include "config_vars.hpp"

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

static void set_up_toolbar(UIContainer& container);

static void GLAPIENTRY gl_message_callback(GLenum source, GLenum type, GLuint id, GLenum severity,
		GLsizei length, const GLchar* message, const void* userParam) {
	if (type == GL_DEBUG_TYPE_ERROR) {
		fprintf(stderr, "GL CALLBACK: **ERROR** type = 0x%x, severity = 0x%x, message = %s\n", type, severity,
				message);
	}
}

int main() {
	if (auto res = Text::FontRegistry::register_families_from_path("fonts/families");
			res != Text::FontRegistryError::NONE) {
		printf("Failed to initialize font registry: %d\n", res);
	}

	auto family = Text::FontRegistry::get_family("Noto Sans");
	Text::Font font(family, Text::FontWeight::REGULAR, Text::FontStyle::NORMAL, 48);

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
	textBox->set_name("TextBox");
	textBox->set_rich_text(true);
	textBox->set_font(font);
	textBox->set_position(INSET, ToolBar::TOOL_BAR_HEIGHT);
	textBox->set_size(g_width - 2 * INSET, g_height - INSET - ToolBar::TOOL_BAR_HEIGHT);
	textBox->set_parent(container.get());

	set_up_toolbar(*container);
	
	{
		std::vector<char> fileData;
		if (fileData = file_read_bytes("Sample.txt"); !fileData.empty()) {
			//std::string str("Hello\tworld");
			//std::string str("یہ ایک )car( ہے۔");
			//std::string str("<hello>");
			//std::string str("	 ̈‫ aaa ‭ אאא ‮ aaa ‪ אאא ‬ aaa ‬ ااا ‬ aaa ‬ aaa ‬&‬‌‌&‬");
			//std::string str(fileData.data(), fileData.size());
			//std::string str("beffiإلابسم اللهafter\r\nhello");
			//std::string str("beffiإلابسم الله\r\nhello");
			//std::string str("beffiإلابسماللهhello");
			//std::string str("beffiإلابسم اللهffter\r\nbeffiإلابسم اللهffter");
			//std::string str("beffiإلابسم اللهffter");
			//std::string str("<font color=\"#FF0000\">beffi</font>إلابسم اللهffter");
			//std::string str("إلابسم الله");
			//std::string str("<font color=\"#FF0000\"><s>إلابسم</s></font> <u>الله</u>");
			//std::string str("A\r\n\r\nB\r\nC\r\n");
			//std::string str("HelloWorld");
			//std::string str("BeeffiWorld");
			//std::string str("aaa\u2067אאא\u2066bbb\u202bבבב\u202cccc\u2069גגג");
			std::string str("Beeffi");
			//std::string str("<u><stroke thickness=\"2\" color=\"#007F00\"><font color=\"#FF0000\">Be<s>e</s></font></stroke><s>ff</s></u><s>i</s>");
			//std::string str("Hello\r\nWorld");
			//std::string str("\xF0\x9F\x87\xAE\xF0\x9F\x87\xB9");
			//std::string str("Hello <font face=\"Noto Sans Synth\" weight=\"bold\">World</font>\nHello <font weight=\"bold\">World</font>");
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

	double lastTime = glfwGetTime();

	while (!glfwWindowShouldClose(window)) {
		double currTime = glfwGetTime();
		auto deltaTime = currTime - lastTime;
		lastTime = currTime;

		container->update(static_cast<float>(deltaTime));
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

	if (auto* pTextBox = pContainer->find_first_child("TextBox")) {
		pTextBox->set_size(static_cast<float>(width) - 2 * INSET, static_cast<float>(height) - INSET);
	}

	if (auto* pToolBar = pContainer->find_first_child("ToolBar")) {
		pToolBar->set_size(static_cast<float>(width), ToolBar::TOOL_BAR_HEIGHT);
	}
}

static void render(UIContainer& container) {
	glClearColor(1.f, 1.f, 1.f, 1.f);
	glClear(GL_COLOR_BUFFER_BIT);
	container.render();
}

static void set_up_toolbar(UIContainer& container) {
	auto toolBar = ToolBar::create(g_width);
	toolBar->set_name("ToolBar");
	toolBar->set_parent(&container);

	auto formatMenu = toolBar->add_menu("Format");

	auto itemUseMSDF = formatMenu->add_item("UseMSDF", "Use MSDF");
	itemUseMSDF->set_clicked_callback([](auto& btn) {
		btn.set_selected(!btn.is_selected());
		CVars::useMSDF = btn.is_selected();
	});

	auto viewMenu = toolBar->add_menu("View");

	auto itemShowGlyphOutlines = viewMenu->add_item("ShowGlyphOutlines", "Show Glyph Outlines");
	itemShowGlyphOutlines->set_clicked_callback([](auto& btn) {
		btn.set_selected(!btn.is_selected());
		CVars::showGlyphOutlines = btn.is_selected();
	});

	auto itemShowRunOutlines = viewMenu->add_item("ShowRunOutlines", "Show Run Outlines");
	itemShowRunOutlines->set_clicked_callback([](auto& btn) {
		btn.set_selected(!btn.is_selected());
		CVars::showRunOutlines = btn.is_selected();
	});

	auto itemShowGlyphBoundaries = viewMenu->add_item("ShowGlyphBoundaries", "Show Glyph Boundaries");
	itemShowGlyphBoundaries->set_clicked_callback([](auto& btn) {
		btn.set_selected(!btn.is_selected());
		CVars::showGlyphBoundaries = btn.is_selected();
	});
}

