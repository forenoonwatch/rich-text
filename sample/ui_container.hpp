#pragma once

#include "ui_object.hpp"

class UIContainer final : public UIObject {
	public:
		static std::shared_ptr<UIContainer> create();

		void render(const float* invScreenSize) override;

		bool handle_key_press(int key, int action, int mods, double mouseX, double mouseY);

		bool handle_mouse_button(int button, int action, int mods, double mouseX, double mouseY) override;
		bool handle_mouse_move(double mouseX, double mouseY) override;
		bool handle_text_input(unsigned codepoint) override;
	private:
};

