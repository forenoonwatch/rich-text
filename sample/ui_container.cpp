#include "ui_container.hpp"

#include "text_atlas.hpp"

std::shared_ptr<UIContainer> UIContainer::create() {
	return std::make_shared<UIContainer>();
}

void UIContainer::emit_rect(float x, float y, float width, float height, const float* texCoords, Image* texture,
		const Color& color, PipelineIndex pipeline, const Text::Pair<float, float>* pClip) {
	if (pClip) {
		// Rect is completely uncovered by clip range, just emit this rect with no clip
		if (x >= pClip->second || x + width <= pClip->first) {
			emit_rect(x, y, width, height, texCoords, texture, color, pipeline);
		}
		// Rect is partially clipped
		else {
			auto newX = x;
			auto newWidth = width;
			auto newUVX = texCoords[0];
			auto newUVWidth = texCoords[2];

			// The left side of the rect is partially unclipped by at least 1px, emit left as normal
			if (pClip->first >= x + 1.f && pClip->first < x + width) {
				auto diff = pClip->first - x;
				newX += diff;
				newWidth -= diff;

				auto tcDiff = texCoords[2] * diff / width;
				newUVX += tcDiff;
				newUVWidth -= tcDiff;

				float texCoordsOut[4] = {texCoords[0], texCoords[1], tcDiff, texCoords[3]};
				emit_rect(x, y, diff, height, texCoordsOut, texture, color, pipeline);
			}

			// The right side of the rect is partially unclipped by at least 1px, emit right as normal
			if (pClip->second > x && pClip->second + 1.f <= x + width) {
				auto diff = x + width - pClip->second;
				newWidth -= diff;

				auto tcDiff = texCoords[2] * diff / width; 
				newUVWidth -= tcDiff;

				float texCoordsOut[4] = {texCoords[0] + texCoords[2] - tcDiff, texCoords[1], tcDiff,
					texCoords[3]};
				emit_rect(x + width - diff, y, diff, height, texCoordsOut, texture, color, pipeline);
			}

			// Result of the intersection is the clipped rect
			float texCoordsOut[4] = {newUVX, texCoords[1], newUVWidth, texCoords[3]};
			emit_rect(newX, y, newWidth, height, texCoordsOut, texture, {1.f, 1.f, 1.f, 1.f}, pipeline);
		}
	}
	else {
		draw_rect_internal(x, y, width, height, texCoords, texture, color, pipeline);
	}
}

void UIContainer::emit_rect(float x, float y, float width, float height, const Color& color,
		PipelineIndex pipeline, const Text::Pair<float, float>* pClip) {
	float texCoords[4] = {0.f, 0.f, 1.f, 1.f};
	emit_rect(x, y, width, height, texCoords, g_textAtlas->get_default_texture(), color, pipeline, pClip);
}

void UIContainer::render() {
	m_pipelineIndex = PipelineIndex::INVALID;
	m_pipeline = nullptr;
	m_texture = nullptr;

	for_each_descendant([&](auto& desc) {
		desc.render(*this);
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

void UIContainer::draw_rect_internal(float x, float y, float width, float height, const float* texCoords,
		Image* texture, const Color& color, PipelineIndex pipeline) {
	if (!texture) {
		return;
	}

	if (pipeline != m_pipelineIndex) {
		m_pipelineIndex = pipeline;
		m_pipeline = &g_pipelines[static_cast<size_t>(pipeline)];
		m_pipeline->bind();

		float invScreenSize[] = {1.f / get_size()[0], 1.f / get_size()[1]};
		m_pipeline->set_uniform_float2(0, invScreenSize);
	}

	if (texture != m_texture) {
		m_texture = texture;
		texture->bind();
	}

	float extents[] = {x, y, width, height};
	m_pipeline->set_uniform_float4(1, extents);
	m_pipeline->set_uniform_float4(2, texCoords);
	m_pipeline->set_uniform_float4(3, reinterpret_cast<const float*>(&color));
	m_pipeline->draw();
}

