#include "tool_bar_menu_item.hpp"

#include "font_registry.hpp"

#include "ui_container.hpp"

#include <GLFW/glfw3.h>

static constexpr const Color HOVER_COLOR = Color::from_rgb(145, 201, 247);
static constexpr const Color CHECK_BOX_COLOR = Color::from_rgb(86, 176, 250);

std::shared_ptr<ToolBarMenuItem> ToolBarMenuItem::create() {
	return std::make_shared<ToolBarMenuItem>();
}

bool ToolBarMenuItem::handle_mouse_button(UIContainer&, int button, int action, int /*mods*/,
		double mouseX, double mouseY) {
	bool active = is_visible() && is_mouse_inside(mouseX, mouseY);

	if (button == GLFW_MOUSE_BUTTON_1 && action == GLFW_PRESS && active && m_clickCallback) {
		m_clickCallback(*this);
	}

	return active;
}

bool ToolBarMenuItem::handle_mouse_move(UIContainer&, double mouseX, double mouseY) {
	m_hovered = is_mouse_inside(mouseX, mouseY);

	return false;
}

void ToolBarMenuItem::render(UIContainer& container) {
	auto family = Text::FontRegistry::get_family("Noto Sans");
	Text::Font font(family, Text::FontWeight::REGULAR, Text::FontStyle::NORMAL, 16);

	if (m_hovered) {
		container.emit_rect(get_absolute_position()[0], get_absolute_position()[1], get_size()[0],
				get_size()[1], HOVER_COLOR, PipelineIndex::RECT);
	}

	if (m_selected) {
		if (m_hovered) {
			container.emit_rect(get_absolute_position()[0], get_absolute_position()[1], get_size()[1],
					get_size()[1], CHECK_BOX_COLOR, PipelineIndex::RECT);
		}

		container.draw_text_immediate(font, {0, 0, 0, 1}, "\u2713", get_absolute_position()[0],
				get_absolute_position()[1], get_size()[1], get_size()[1], TextXAlignment::CENTER,
				TextYAlignment::CENTER);
	}

	container.draw_text_immediate(font, {0, 0, 0, 1}, m_text, get_absolute_position()[0] + TEXT_OFFSET,
			get_absolute_position()[1], get_size()[0], get_size()[1], TextXAlignment::LEFT,
			TextYAlignment::CENTER);
}

void ToolBarMenuItem::set_text(std::string text) {
	m_text = std::move(text);
}

void ToolBarMenuItem::set_selected(bool selected) {
	m_selected = selected;
}

bool ToolBarMenuItem::is_selected() const {
	return m_selected;
}

