#include "tool_bar.hpp"

#include "tool_bar_menu.hpp"
#include "ui_container.hpp"

static constexpr const float UNDERLINE_THICKNESS = 2.f;
static constexpr const Text::Color UNDERLINE_COLOR = Text::Color::from_rgb(240, 240, 240);

std::shared_ptr<ToolBar> ToolBar::create(float width) {
	auto tb = std::make_shared<ToolBar>();
	tb->set_position(0, 0);
	tb->set_size(width, TOOL_BAR_HEIGHT);
	return tb;
}

bool ToolBar::handle_mouse_move(UIContainer&, double mouseX, double mouseY) {
	if (m_menusOpen && is_mouse_inside(mouseX, mouseY)) {
		ToolBarMenu* pFoundMenu = nullptr;

		for_each_child([&](auto& child) {
			auto& menu = static_cast<ToolBarMenu&>(child);

			if (menu.is_mouse_inside(mouseX, mouseY)) {
				pFoundMenu = &menu;
				return IterationDecision::BREAK;
			}

			return IterationDecision::CONTINUE;
		});

		if (pFoundMenu) {
			pFoundMenu->set_open(true);

			for_each_child([&](auto& child) {
				auto& menu = static_cast<ToolBarMenu&>(child);

				if (&child != pFoundMenu) {
					menu.set_open(false);
				}

				menu.m_hovered = false;

				return IterationDecision::CONTINUE;
			});
		}
	}
	else if (!m_menusOpen) {
		for_each_child([&](auto& child) {
			auto& menu = static_cast<ToolBarMenu&>(child);
			menu.m_hovered = menu.is_mouse_inside(mouseX, mouseY);
			return IterationDecision::CONTINUE;
		});
	}

	return false;
}

void ToolBar::render(UIContainer& container) {
	// Underline
	container.emit_rect(get_absolute_position()[0], get_absolute_position()[1] + get_size()[1]
			- UNDERLINE_THICKNESS,  get_size()[0], UNDERLINE_THICKNESS, UNDERLINE_COLOR, PipelineIndex::RECT);
}

std::shared_ptr<ToolBarMenu> ToolBar::add_menu(std::string name) {
	auto menu = ToolBarMenu::create(std::move(name));

	float offset = 0.f;

	for_each_child([&](auto& child) {
		offset += child.get_size()[0];
		return IterationDecision::CONTINUE;
	});

	menu->set_position(offset, 0);
	menu->set_parent(this);

	return menu;
}

void ToolBar::set_menus_open(bool menusOpen) {
	m_menusOpen = menusOpen;
}

bool ToolBar::are_menus_open() const {
	return m_menusOpen;
}

