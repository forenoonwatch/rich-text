#pragma once

#include "iteration_decision.hpp"

#include <memory>
#include <string>
#include <string_view>

class UIContainer;

class UIObject : public std::enable_shared_from_this<UIObject> {
	public:
		virtual ~UIObject() = default;

		virtual void update(float deltaTime);
		virtual void render(UIContainer&);

		virtual bool handle_mouse_button(UIContainer&, int button, int action, int mods, double mouseX,
				double mouseY);
		virtual bool handle_key_press(UIContainer&, int key, int action, int mods);
		virtual bool handle_mouse_move(UIContainer&, double mouseX, double mouseY);
		virtual bool handle_text_input(UIContainer&, unsigned codepoint);

		virtual void handle_focused(UIContainer&);
		virtual void handle_focus_lost(UIContainer&);

		virtual void handle_mouse_enter(UIContainer&, double mouseX, double mouseY);
		virtual void handle_mouse_leave(UIContainer&);

		UIObject(UIObject&&) = delete;
		void operator=(UIObject&&) = delete;

		UIObject(const UIObject&) = delete;
		void operator=(const UIObject&) = delete;

		void set_parent(UIObject*);
		void set_position(float x, float y);
		void set_name(std::string name);
		virtual void set_size(float width, float height);
		void set_visible(bool);

		std::shared_ptr<UIObject> get_parent() const;

		const float* get_position() const;
		const float* get_size() const;
		const float* get_absolute_position() const;
		bool is_mouse_inside(float mouseX, float mouseY) const;

		const std::string& get_name() const;
		std::string get_full_name() const;

		bool is_visible() const;
		bool is_focused() const;
		bool is_visible_from_ancestors() const;

		UIObject* find_first_child(std::string_view name) const;

		template <typename Functor>
		IterationDecision for_each_child(Functor&& func);
		template <typename Functor>
		IterationDecision for_each_child(Functor&& func) const;

		template <typename Functor>
		IterationDecision for_each_child_reverse(Functor&& func);
		template <typename Functor>
		IterationDecision for_each_child_reverse(Functor&& func) const;

		template <typename Functor>
		IterationDecision for_each_descendant(Functor&& func);
		template <typename Functor>
		IterationDecision for_each_descendant(Functor&& func) const;

		template <typename Functor>
		IterationDecision for_each_descendant_bottom_up(Functor&& func);
		template <typename Functor>
		IterationDecision for_each_descendant_bottom_up(Functor&& func) const;

		template <typename Functor>
		IterationDecision for_each_ancestor(Functor&& func) const;
	protected:
		explicit UIObject() = default;

		void recalc_absolute_dimensions();
	private:
		float m_position[2]{};
		float m_size[2]{};

		float m_absolutePosition[2]{};

		std::weak_ptr<UIObject> m_parent{};
		std::shared_ptr<UIObject> m_firstChild{};
		std::shared_ptr<UIObject> m_nextChild{};
		std::weak_ptr<UIObject> m_prevChild{};
		std::weak_ptr<UIObject> m_lastChild{};

		std::string m_name{"UIObject"};

		bool m_visible{true};
		bool m_focused{false};

		void render_internal(UIContainer&);

		friend class UIContainer;
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
IterationDecision UIObject::for_each_child_reverse(Functor&& func) {
	auto pPrevChild = m_lastChild.lock();

	while (pPrevChild) {
		if (func(*pPrevChild) == IterationDecision::BREAK) {
			return IterationDecision::BREAK;
		}

		pPrevChild = pPrevChild->m_prevChild.lock();
	}

	return IterationDecision::CONTINUE;
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

template <typename Functor>
IterationDecision UIObject::for_each_descendant_bottom_up(Functor&& func) {
	return for_each_child_reverse([&](auto& child) {
		if (child.for_each_descendant_bottom_up(func) == IterationDecision::BREAK) {
			return IterationDecision::BREAK;
		}

		return func(child);
	});
}

template <typename Functor>
IterationDecision UIObject::for_each_ancestor(Functor&& func) const {
	auto pParent = m_parent.lock();

	while (pParent) {
		if (func(*pParent) == IterationDecision::BREAK) {
			return IterationDecision::BREAK;
		}

		pParent = pParent->m_parent.lock();
	}

	return IterationDecision::CONTINUE;
}

