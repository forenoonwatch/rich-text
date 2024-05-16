#include "ui_object.hpp"

void UIObject::render(UIContainer& /*container*/) {
}

bool UIObject::handle_mouse_button(UIContainer& /*container*/, int /*button*/, int /*action*/, int /*mods*/,
		double /*mouseX*/, double /*mouseY*/) {
	return false;
}

bool UIObject::handle_key_press(UIContainer& /*container*/, int /*key*/, int /*action*/, int /*mods*/) {
	return false;
}

bool UIObject::handle_mouse_move(UIContainer& /*container*/, double /*mouseX*/, double /*mouseY*/) {
	return false;
}

bool UIObject::handle_text_input(UIContainer& /*container*/, unsigned /*codepoint*/) {
	return false;
}

void UIObject::handle_focused(UIContainer& /*container*/) {}

void UIObject::handle_focus_lost(UIContainer& /*container*/) {}

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

void UIObject::set_name(std::string name) {
	m_name = std::move(name);
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

const std::string& UIObject::get_name() const {
	return m_name;
}

bool UIObject::is_focused() const {
	return m_focused;
}

UIObject* UIObject::find_first_child(std::string_view name) const {
	UIObject* result = nullptr;

	for_each_child([&](UIObject& child) {
		if (name.compare(child.get_name()) == 0) {
			result = &child;
			return IterationDecision::BREAK;
		}

		return IterationDecision::CONTINUE;
	});

	return result;
}

bool UIObject::is_mouse_inside(float mouseX, float mouseY) const {
	return mouseX >= m_position[0] && mouseY >= m_position[1] && mouseX - m_position[0] <= m_size[0]
			&& mouseY - m_position[1] <= m_size[1];
}

