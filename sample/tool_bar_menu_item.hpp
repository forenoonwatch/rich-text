#pragma once

#include "ui_object.hpp"

#include <functional>

class ToolBarMenuItem final : public UIObject {
	public:
		static constexpr const float ITEM_WIDTH = 166.f;
		static constexpr const float ITEM_HEIGHT = 22.f;
		static constexpr const float TEXT_OFFSET = 32.f;

		static std::shared_ptr<ToolBarMenuItem> create();

		bool handle_mouse_button(UIContainer&, int button, int action, int mods, double mouseX,
				double mouseY) override;
		bool handle_mouse_move(UIContainer&, double mouseX, double mouseY) override;

		void render(UIContainer&) override;

		void set_text(std::string);
		void set_selected(bool);

		template <typename Functor>
		void set_clicked_callback(Functor&& func) {
			m_clickCallback = std::forward<Functor>(func);
		}

		bool is_selected() const;
	private:
		std::string m_text;
		bool m_hovered{false};
		bool m_selected{false};

		std::function<void(ToolBarMenuItem&)> m_clickCallback;
};

