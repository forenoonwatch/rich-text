#pragma once

#include "iteration_decision.hpp"

#include <memory>

class UIObject : public std::enable_shared_from_this<UIObject> {
	public:
		virtual ~UIObject() = default;

		virtual void render(const float* invScreenSize);

		virtual bool handle_mouse_button(int button, int action, int mods, double mouseX, double mouseY);
		virtual bool handle_key_press(int key, int action, int mods);
		virtual bool handle_mouse_move(double mouseX, double mouseY);
		virtual bool handle_text_input(unsigned codepoint);

		UIObject(UIObject&&) = delete;
		void operator=(UIObject&&) = delete;

		UIObject(const UIObject&) = delete;
		void operator=(const UIObject&) = delete;

		void set_parent(UIObject*);
		void set_position(float x, float y);
		void set_size(float width, float height);

		const float* get_position() const;
		const float* get_size() const;
		bool is_mouse_inside(float mouseX, float mouseY) const;

		template <typename Functor>
		IterationDecision for_each_child(Functor&& func);
		template <typename Functor>
		IterationDecision for_each_child(Functor&& func) const;

		template <typename Functor>
		IterationDecision for_each_descendant(Functor&& func);
		template <typename Functor>
		IterationDecision for_each_descendant(Functor&& func) const;
	protected:
		explicit UIObject() = default;
	private:
		float m_position[2]{};
		float m_size[2]{};

		std::weak_ptr<UIObject> m_parent{};
		std::shared_ptr<UIObject> m_firstChild{};
		std::shared_ptr<UIObject> m_nextChild{};
		std::weak_ptr<UIObject> m_prevChild{};
		std::weak_ptr<UIObject> m_lastChild{};
};

template <typename Functor>
IterationDecision UIObject::for_each_child(Functor&& func) {
	auto* nextChild = m_firstChild.get();

	while (nextChild) {
		if (func(*nextChild) == IterationDecision::BREAK) {
			return IterationDecision::BREAK;
		}

		nextChild = nextChild->m_nextChild.get();
	}

	return IterationDecision::CONTINUE;
}

template <typename Functor>
IterationDecision UIObject::for_each_child(Functor&& func) const {
	return const_cast<UIObject*>(this)->for_each_child(std::forward<Functor>(func));
}

template <typename Functor>
IterationDecision UIObject::for_each_descendant(Functor&& func) {
	return for_each_child([&](auto& child) {
		if (func(child) == IterationDecision::BREAK) {
			return IterationDecision::BREAK;
		}

		return child.for_each_descendant(func);
	});
}

