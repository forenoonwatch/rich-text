#include "frame.hpp"

#include "ui_container.hpp"

std::shared_ptr<Frame> Frame::create() {
	return std::make_shared<Frame>();
}

void Frame::render(UIContainer& container) {
	container.emit_rect(get_absolute_position()[0], get_absolute_position()[1], get_size()[0], get_size()[1],
			m_backgroundColor, PipelineIndex::RECT);
	container.emit_rect(get_absolute_position()[0], get_absolute_position()[1], get_size()[0], get_size()[1],
			m_borderColor, PipelineIndex::OUTLINE);
}

void Frame::set_background_color(const Text::Color& color) {
	m_backgroundColor = color;
}

void Frame::set_border_color(const Text::Color& color) {
	m_borderColor = color;
}

