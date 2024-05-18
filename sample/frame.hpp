#pragma once

#include "color.hpp"

#include "ui_object.hpp"

class Frame final : public UIObject {
	public:
		static std::shared_ptr<Frame> create();

		void render(UIContainer&) override;

		void set_background_color(const Color&);
		void set_border_color(const Color&);
	private:
		Color m_backgroundColor{0, 0, 0, 1};
		Color m_borderColor{0, 0, 0, 1};
};

