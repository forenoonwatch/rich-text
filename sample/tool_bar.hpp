#pragma once

#include "ui_object.hpp"

class ToolBarMenu;

class ToolBar final : public UIObject {
	public:
		static constexpr const float TOOL_BAR_HEIGHT = 20.f;

		static std::shared_ptr<ToolBar> create(float width);

		bool handle_mouse_move(UIContainer&, double mouseX, double mouseY) override;

		void render(UIContainer&) override;

		std::shared_ptr<ToolBarMenu> add_menu(std::string name);

		void set_menus_open(bool);

		bool are_menus_open() const;
	private:
		bool m_menusOpen = false;
};

