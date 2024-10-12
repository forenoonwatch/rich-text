#pragma once

#include "cursor_controller.hpp"
#include "layout_info.hpp"
#include "formatting.hpp"
#include "ui_object.hpp"

class TextBox final : public UIObject {
	public:
		static std::shared_ptr<TextBox> create();

		bool handle_mouse_button(UIContainer&, int button, int action, int mods, double mouseX,
				double mouseY) override;
		bool handle_key_press(UIContainer&, int key, int action, int mods) override;
		bool handle_mouse_move(UIContainer&, double mouseX, double mouseY) override;
		bool handle_text_input(UIContainer&, unsigned codepoint) override;

		void handle_focused(UIContainer&) override;
		void handle_focus_lost(UIContainer&) override;

		void render(UIContainer&) override;

		void set_size(float width, float height) override;

		void set_font(Text::Font);
		void set_text(std::string);
		void set_text_x_alignment(Text::XAlignment);
		void set_text_y_alignment(Text::YAlignment);
		void set_text_wrapped(bool);
		void set_multi_line(bool);
		void set_rich_text(bool);
		void set_editable(bool);
		void set_selectable(bool);
	private:
		Text::Font m_font{};
		std::string m_text{};
		std::string m_contentText{};
		Text::Color m_textColor{0.f, 0.f, 0.f, 1.f};
		Text::CursorPosition m_cursorPosition{};
		Text::CursorPosition m_selectionStart{Text::CursorPosition::INVALID_VALUE};
		Text::XAlignment m_textXAlignment{Text::XAlignment::LEFT};
		Text::YAlignment m_textYAlignment{Text::YAlignment::TOP};
		bool m_textWrapped = true;
		bool m_multiLine = true;
		bool m_richText = false;
		bool m_editable = true;
		bool m_selectable = true;
		bool m_dragSelecting = false;

		Text::LayoutInfo m_layout;
		Text::FormattingRuns m_formatting;
		Text::VisualCursorInfo m_visualCursorInfo;
		Text::CursorController m_cursorCtrl;

		bool should_focused_use_rich_text() const;

		void cursor_move_to_next_character(bool selectionMode);
		void cursor_move_to_prev_character(bool selectionMode);

		void cursor_move_to_next_word(bool selectionMode);
		void cursor_move_to_prev_word(bool selectionMode);

		void cursor_move_to_next_line(bool selectionMode);
		void cursor_move_to_prev_line(bool selectionMode);

		void cursor_move_to_line_start(bool selectionMode);
		void cursor_move_to_line_end(bool selectionMode);

		void cursor_move_to_text_start(bool selectionMode);
		void cursor_move_to_text_end(bool selectionMode);

		void cursor_move_to_mouse(double mouseX, double mouseY, bool selectionMode);

		void set_cursor_position_internal(Text::CursorPosition pos, bool selectionMode);

		void handle_key_backspace(bool ctrl);
		void handle_key_delete(bool ctrl);
		void handle_key_enter(UIContainer&);
		void handle_key_tab();

		void clipboard_cut_text();
		void clipboard_copy_text();
		void clipboard_paste_text();

		void insert_typed_character(unsigned codepoint);
		void insert_text(const std::string& text, uint32_t startIndex);
		void remove_text(uint32_t startIndex, uint32_t endIndex);
		void remove_highlighted_text();

		void recalc_text();
};

