#pragma once

#include <layout_builder.hpp>

#include "color.hpp"
#include "cursor_position.hpp"
#include "font.hpp"
#include "pair.hpp"
#include "pipeline.hpp"
#include "text_alignment.hpp"
#include "ui_object.hpp"

#include <bitset>

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
				const Text::Color& color, PipelineIndex pipeline,
				const Text::Pair<float, float>* pClip = nullptr);
		void emit_rect(float x, float y, float width, float height, const Text::Color& color,
				PipelineIndex pipeline, const Text::Pair<float, float>* pClip = nullptr);

		void draw_text(const Text::LayoutInfo&, float x, float y, float width, Text::XAlignment,
				const Text::Color& color);
		void draw_text(const Text::LayoutInfo&, const Text::FormattingRuns&, float x, float y, float width,
				float height, Text::XAlignment, Text::YAlignment, bool vertical,
				Text::CursorPosition selectionStart = {}, Text::CursorPosition cursorPosition = {});

		void draw_text_immediate(Text::Font font, const Text::Color& color, std::string_view text, float x, 
				float y, float width, float height, Text::XAlignment, Text::YAlignment);

		void focus_object(UIObject&);
		void release_focused_object();

		std::weak_ptr<UIObject> get_focused_object() const;

		double get_mouse_x() const;
		double get_mouse_y() const;

		bool is_mouse_button_down(int mouseButton) const;
	protected:
		uint32_t text_box_click(Text::CursorPosition);

		friend TextBox;
	private:
		PipelineIndex m_pipelineIndex{PipelineIndex::INVALID};
		Pipeline* m_pipeline{};
		Image* m_texture{};

		std::weak_ptr<UIObject> m_focusedObject{};
		std::weak_ptr<UIObject> m_hoveredObject{};

		double m_mouseX{};
		double m_mouseY{};

		std::bitset<8> m_mouseButtonsDown;

		// TextBox control info
		double m_lastClickTime{};
		uint32_t m_clickCount{};
		Text::CursorPosition m_lastClickPos{Text::CursorPosition::INVALID_VALUE};

		Text::LayoutBuilder m_layoutBuilder;

		void draw_rect_internal(float x, float y, float width, float height, const float* texCoords,
				Image* texture, const Text::Color& color, PipelineIndex pipeline);
};

