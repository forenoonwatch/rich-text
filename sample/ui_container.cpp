#include "ui_container.hpp"

std::shared_ptr<UIContainer> UIContainer::create() {
	return std::make_shared<UIContainer>();
}

void UIContainer::render(const float* invScreenSize) {
	for_each_descendant([&](auto& desc) {
		desc.render(invScreenSize);
		return IterationDecision::CONTINUE;
	});
}

bool UIContainer::handle_mouse_button(int button, int action, int mods, double mouseX, double mouseY) {
	auto iterDec = for_each_descendant([&](auto& desc) {
		if (desc.is_mouse_inside(mouseX, mouseY)) {
			return desc.handle_mouse_button(button, action, mods, mouseX, mouseY) ? IterationDecision::BREAK
					: IterationDecision::CONTINUE;
		}
		
		return IterationDecision::CONTINUE;
	});

	return iterDec == IterationDecision::BREAK;
}

bool UIContainer::handle_key_press(int key, int action, int mods, double mouseX, double mouseY) {
	auto iterDec = for_each_descendant([&](auto& desc) {
		if (desc.is_mouse_inside(mouseX, mouseY)) {
			return desc.handle_key_press(key, action, mods) ? IterationDecision::BREAK
					: IterationDecision::CONTINUE;
		}
		
		return IterationDecision::CONTINUE;
	});

	return iterDec == IterationDecision::BREAK;
}

bool UIContainer::handle_mouse_move(double mouseX, double mouseY) {
	auto iterDec = for_each_descendant([&](auto& desc) {
		if (desc.is_mouse_inside(mouseX, mouseY)) {
			return desc.handle_mouse_move(mouseX, mouseY) ? IterationDecision::BREAK
					: IterationDecision::CONTINUE;
		}
		
		return IterationDecision::CONTINUE;
	});

	return iterDec == IterationDecision::BREAK;
}

bool UIContainer::handle_text_input(unsigned codepoint) {
	auto iterDec = for_each_descendant([&](auto& desc) {
		return desc.handle_text_input(codepoint) ? IterationDecision::BREAK : IterationDecision::CONTINUE;
	});

	return iterDec == IterationDecision::BREAK;
}

