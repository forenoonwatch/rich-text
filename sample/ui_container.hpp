#pragma once

#include "color.hpp"
#include "cursor_position.hpp"
#include "pair.hpp"
#include "pipeline.hpp"
#include "text_alignment.hpp"
#include "ui_object.hpp"

class TextBox;

namespace Text { struct FormattingRuns; }
namespace Text { class LayoutInfo; }

class UIContainer final : public UIObject {
	public:
		static constexpr const double DOUBLE_CLICK_TIME = 0.5;

		static std::shared_ptr<UIContainer> create();

		void render();

		bool handle_mouse_button(int button, int action, int mods, double mouseX, double mouseY);
		bool handle_key_press(int key, int action, int mods, double mouseX, double mouseY);
		bool handle_mouse_move(double mouseX, double mouseY);
		bool handle_text_input(unsigned codepoint);

		void handle_focus_lost();

		void emit_rect(float x, float y, float width, float height, const float* texCoords, Image* texture,
				const Color& color, PipelineIndex pipeline,
				const Text::Pair<float, float>* pClip = nullptr);
		void emit_rect(float x, float y, float width, float height, const Color& color, PipelineIndex pipeline,
				const Text::Pair<float, float>* pClip = nullptr);

		void draw_text(const Text::LayoutInfo&, const Text::FormattingRuns&, float x, float y, float width,
				TextXAlignment, CursorPosition selectionStart = {}, CursorPosition cursorPosition = {});

		void focus_object(UIObject&);
		void release_focused_object();

		std::weak_ptr<UIObject> get_focused_object() const;
	protected:
		uint32_t text_box_click(CursorPosition);

		friend TextBox;
	private:
		PipelineIndex m_pipelineIndex{PipelineIndex::INVALID};
		Pipeline* m_pipeline{};
		Image* m_texture{};

		std::weak_ptr<UIObject> m_focusedObject{};

		// TextBox control info
		double m_lastClickTime{};
		uint32_t m_clickCount{};
		CursorPosition m_lastClickPos{CursorPosition::INVALID_VALUE};

		void draw_rect_internal(float x, float y, float width, float height, const float* texCoords,
				Image* texture, const Color& color, PipelineIndex pipeline);
};

