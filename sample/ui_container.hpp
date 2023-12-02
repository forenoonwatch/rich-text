#pragma once

#include "color.hpp"
#include "pair.hpp"
#include "pipeline.hpp"
#include "ui_object.hpp"

class UIContainer final : public UIObject {
	public:
		static std::shared_ptr<UIContainer> create();

		void render();

		bool handle_key_press(int key, int action, int mods, double mouseX, double mouseY);

		bool handle_mouse_button(int button, int action, int mods, double mouseX, double mouseY) override;
		bool handle_mouse_move(double mouseX, double mouseY) override;
		bool handle_text_input(unsigned codepoint) override;

		void emit_rect(float x, float y, float width, float height, const float* texCoords, Image* texture,
				const Color& color, PipelineIndex pipeline,
				const Text::Pair<float, float>* pClip = nullptr);
		void emit_rect(float x, float y, float width, float height, const Color& color, PipelineIndex pipeline,
				const Text::Pair<float, float>* pClip = nullptr);
	private:
		PipelineIndex m_pipelineIndex{PipelineIndex::INVALID};
		Pipeline* m_pipeline{};
		Image* m_texture{};

		void draw_rect_internal(float x, float y, float width, float height, const float* texCoords,
				Image* texture, const Color& color, PipelineIndex pipeline);
};

