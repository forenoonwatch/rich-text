#pragma once

#include "multi_script_font.hpp"
#include "layout_info.hpp"
#include "formatting.hpp"
#include "ui_object.hpp"

class TextBox final : public UIObject {
	public:
		static std::shared_ptr<TextBox> create();

		static TextBox* get_focused_text_box();

		TextBox() = default;
		~TextBox();

		bool handle_mouse_button(int button, int action, int mods, double mouseX, double mouseY) override;
		bool handle_key_press(int key, int action, int mods) override;
		bool handle_mouse_move(double mouseX, double mouseY) override;
		bool handle_text_input(unsigned codepoint) override;

		void capture_focus();
		void release_focus();

		void render(UIContainer&) override;

		void set_font(MultiScriptFont);
		void set_text(std::string);
		void set_text_x_alignment(TextXAlignment);
		void set_text_y_alignment(TextYAlignment);
		void set_text_wrapped(bool);
		void set_multi_line(bool);
		void set_rich_text(bool);
		void set_editable(bool);
		void set_selectable(bool);

		bool is_focused() const;
	private:
		MultiScriptFont m_font{};
		std::string m_text{};
		std::string m_contentText{};
		Color m_textColor{0.f, 0.f, 0.f, 1.f};
		CursorPosition m_cursorPosition{};
		CursorPosition m_selectionStart{CursorPosition::INVALID_VALUE};
		TextXAlignment m_textXAlignment{TextXAlignment::LEFT};
		TextYAlignment m_textYAlignment{TextYAlignment::TOP};
		bool m_textWrapped = true;
		bool m_multiLine = true;
		bool m_richText = false;
		bool m_editable = true;
		bool m_selectable = true;

		Text::LayoutInfo m_layout;
		Text::FormattingRuns m_formatting;
		Text::VisualCursorInfo m_visualCursorInfo;

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

		void set_cursor_position_internal(CursorPosition pos, bool selectionMode);

		void handle_key_backspace(bool ctrl);
		void handle_key_delete(bool ctrl);
		void handle_key_enter();

		void clipboard_cut_text();
		void clipboard_copy_text();
		void clipboard_paste_text();

		void insert_text(const std::string& text, uint32_t startIndex);
		void remove_text(uint32_t startIndex, uint32_t endIndex);
		void remove_highlighted_text();

		void recalc_text();
		void recalc_text_internal(bool richText, const void* postLayoutOp);
};

