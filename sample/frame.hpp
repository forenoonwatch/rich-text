#pragma once

#include "color.hpp"

#include "ui_object.hpp"

class Frame final : public UIObject {
	public:
		static std::shared_ptr<Frame> create();

		void render(UIContainer&) override;

		void set_background_color(const Text::Color&);
		void set_border_color(const Text::Color&);
	private:
		Text::Color m_backgroundColor{0, 0, 0, 1};
		Text::Color m_borderColor{0, 0, 0, 1};
};

