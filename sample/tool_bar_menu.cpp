#include "tool_bar_menu.hpp"

#include "font_registry.hpp"
#include "tool_bar.hpp"
#include "tool_bar_menu_item.hpp"

#include "ui_container.hpp"

#include <GLFW/glfw3.h>

static constexpr const Text::Color HOVER_COLOR = Text::Color::from_rgb(211, 224, 255, 127);
static constexpr const Text::Color HOVER_BORDER_COLOR = Text::Color::from_rgb(153, 209, 255, 127);

static constexpr const Text::Color SELECTED_COLOR = Text::Color::from_rgb(153, 209, 255, 127);
static constexpr const Text::Color SELECTED_BORDER_COLOR = Text::Color::from_rgb(51, 163, 255, 127);

static constexpr const Text::Color TRAY_COLOR = Text::Color::from_rgb(242, 242, 242);
static constexpr const Text::Color TRAY_BORDER_COLOR = Text::Color::from_rgb(204, 204, 204);

std::shared_ptr<ToolBarMenu> ToolBarMenu::create(std::string name) {
	auto tb = std::make_shared<ToolBarMenu>();
	tb->set_size(10.f * name.size(), ToolBar::TOOL_BAR_HEIGHT - 1.f);
	tb->set_name(std::move(name));

	tb->m_tray = Frame::create();
	tb->m_tray->set_background_color(TRAY_COLOR);
	tb->m_tray->set_border_color(TRAY_BORDER_COLOR);
	tb->m_tray->set_position(0, ToolBar::TOOL_BAR_HEIGHT);
	tb->m_tray->set_size(BASE_MENU_WIDTH - 1, 2 * PADDING - 1);
	tb->m_tray->set_visible(false);
	tb->m_tray->set_parent(tb.get());

	return tb;
}

bool ToolBarMenu::handle_mouse_button(UIContainer&, int button, int action, int /*mods*/, double mouseX,
		double mouseY) {
	if (button == GLFW_MOUSE_BUTTON_1 && is_mouse_inside(mouseX, mouseY)) {
		if (action == GLFW_PRESS) {
			if (auto pToolBar = get_parent()) {
				auto& toolBar = static_cast<ToolBar&>(*pToolBar);
				toolBar.set_menus_open(!toolBar.are_menus_open());

				if (toolBar.are_menus_open()) {
					set_open(true);
					m_hovered = false;
				}
				else {
					set_open(false);
					m_hovered = true;
				}
			}
		}

		return true;
	}

	return false;
}

void ToolBarMenu::render(UIContainer& container) {
	auto family = Text::FontRegistry::get_family("Noto Sans");
	Text::Font font(family, Text::FontWeight::REGULAR, Text::FontStyle::NORMAL, 16);

	// Button coloring
	if (m_open) {
		container.emit_rect(get_absolute_position()[0] + 1, get_absolute_position()[1] + 1, get_size()[0] - 2,
				get_size()[1] - 2, SELECTED_COLOR, PipelineIndex::RECT);
		container.emit_rect(get_absolute_position()[0], get_absolute_position()[1], get_size()[0] - 1,
				get_size()[1] - 1, SELECTED_BORDER_COLOR, PipelineIndex::OUTLINE);
	}
	else if (m_hovered) {
		container.emit_rect(get_absolute_position()[0] + 1, get_absolute_position()[1] + 1, get_size()[0] - 2,
				get_size()[1] - 2, HOVER_COLOR, PipelineIndex::RECT);
		container.emit_rect(get_absolute_position()[0], get_absolute_position()[1], get_size()[0] - 1,
				get_size()[1] - 1, HOVER_BORDER_COLOR, PipelineIndex::OUTLINE);
	}

	container.draw_text_immediate(font, Text::Color{0.f, 0.f, 0.f, 1.f}, get_name(), get_absolute_position()[0],
			get_absolute_position()[1], get_size()[0], get_size()[1], Text::XAlignment::CENTER,
			Text::YAlignment::CENTER);
}

std::shared_ptr<ToolBarMenuItem> ToolBarMenu::add_item(std::string name, std::string text) {
	auto item = ToolBarMenuItem::create();
	item->set_name(std::move(name));
	item->set_text(std::move(text));
	item->set_position(PADDING, m_tray->get_size()[1] - PADDING + 1);
	item->set_size(ToolBarMenuItem::ITEM_WIDTH, ToolBarMenuItem::ITEM_HEIGHT);
	item->set_parent(m_tray.get());

	m_tray->set_size(m_tray->get_size()[0], m_tray->get_size()[1] + ToolBarMenuItem::ITEM_HEIGHT);

	return item;
}

void ToolBarMenu::set_open(bool open) {
	m_open = open;
	m_tray->set_visible(open);
}

