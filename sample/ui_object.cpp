#include "ui_object.hpp"

void UIObject::render(const float* /*invScreenSize*/) {
}

bool UIObject::handle_mouse_button(int /*button*/, int /*action*/, int /*mods*/, double /*mouseX*/,
		double /*mouseY*/) {
	return false;
}

bool UIObject::handle_key_press(int /*key*/, int /*action*/, int /*mods*/) {
	return false;
}

bool UIObject::handle_mouse_move(double /*mouseX*/, double /*mouseY*/) {
	return false;
}

bool UIObject::handle_text_input(unsigned /*codepoint*/) {
	return false;
}

void UIObject::set_parent(UIObject* newParent) {
	auto oldParent = m_parent.lock();

	if (newParent == oldParent.get() || newParent == this) {
		return;
	}

	m_parent = newParent ? newParent->weak_from_this() : std::weak_ptr<UIObject>();

	// Fix up old parent's child list
	if (oldParent) {
		if (oldParent->m_firstChild.get() == this) {
			oldParent->m_firstChild = m_nextChild;
		}
		else if (auto prevChild = m_prevChild.lock()) {
			prevChild->m_nextChild = m_nextChild;
		}

		if (auto lastChild = oldParent->m_lastChild.lock(); lastChild.get() == this) {
			oldParent->m_lastChild = m_nextChild;
		}
	}

	// Register self in new parent's list
	if (newParent) {
		m_prevChild = newParent->m_lastChild;

		if (auto lastChild = newParent->m_lastChild.lock()) {
			lastChild->m_nextChild = shared_from_this();
		}
		else {
			newParent->m_firstChild = shared_from_this();
		}

		newParent->m_lastChild = weak_from_this();
	}
}

void UIObject::set_position(float x, float y) {
	m_position[0] = x;
	m_position[1] = y;
}

void UIObject::set_size(float width, float height) {
	m_size[0] = width;
	m_size[1] = height;
}

const float* UIObject::get_position() const {
	return m_position;
}

const float* UIObject::get_size() const {
	return m_size;
}

bool UIObject::is_mouse_inside(float mouseX, float mouseY) const {
	return mouseX >= m_position[0] && mouseY >= m_position[1] && mouseX - m_position[0] <= m_size[0]
			&& mouseY - m_position[1] <= m_size[1];
}

