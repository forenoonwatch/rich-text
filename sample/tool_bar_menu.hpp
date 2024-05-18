#pragma once

#include "frame.hpp"

class ToolBarMenuItem;

class ToolBarMenu final : public UIObject {
	public:
		static constexpr const float BASE_MENU_WIDTH = 172.f;
		static constexpr const float PADDING = 3.f;

		static std::shared_ptr<ToolBarMenu> create(std::string name);

		bool handle_mouse_button(UIContainer&, int button, int action, int mods, double mouseX,
				double mouseY) override;

		void render(UIContainer&) override;

		std::shared_ptr<ToolBarMenuItem> add_item(std::string name, std::string text);
	private:
		std::shared_ptr<Frame> m_tray;
		bool m_open = false;
		bool m_hovered = false;

		void set_open(bool);

		friend class ToolBar;
};

