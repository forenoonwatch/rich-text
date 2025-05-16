#include "text_box.hpp"

#include "font_registry.hpp"
#include "layout_builder.hpp"
#include "ui_container.hpp"

#include <GLFW/glfw3.h>

std::shared_ptr<TextBox> TextBox::create() {
	return std::make_shared<TextBox>();
}

bool TextBox::handle_mouse_button(UIContainer& container, int button, int action, int mods, double mouseX,
		double mouseY) {
	if (button != GLFW_MOUSE_BUTTON_1) {
		return false;
	}

	if (action == GLFW_PRESS && is_mouse_inside(mouseX, mouseY)) {
		if (is_focused()) {
			cursor_move_to_mouse(mouseX - get_absolute_position()[0], mouseY - get_absolute_position()[1],
					mods & GLFW_MOD_SHIFT);

			switch (container.text_box_click(m_cursorPosition) % 4) {
				// Highlight Current Word
				case 1:
					cursor_move_to_prev_word(false);
					cursor_move_to_next_word(true);
					break;
				// Highlight Current Line
				case 2:
					cursor_move_to_line_start(false);
					cursor_move_to_line_end(true);
					break;
				// Highlight Whole Text
				case 3:
					cursor_move_to_text_start(false);
					cursor_move_to_text_end(true);
					break;
				default:
					break;
			}
		}
		else {
			// FIXME: the recalc_text in handle_focused is now unnecessary I think
			recalc_text();
			cursor_move_to_mouse(mouseX - get_absolute_position()[0], mouseY - get_absolute_position()[1],
					mods & GLFW_MOD_SHIFT);
		}

		m_dragSelecting = true;

		return true;
	}
	else if (action == GLFW_RELEASE) {
		if (is_focused()) {
			m_dragSelecting = false;
		}
	}

	return false;
}

bool TextBox::handle_key_press(UIContainer& container, int key, int action, int mods) {
	if (action == GLFW_RELEASE) {
		return false;
	}

	if (is_focused()) {
		bool selectionMode = mods & GLFW_MOD_SHIFT;

		switch (key) {
			case GLFW_KEY_UP:
				cursor_move_to_prev_line(selectionMode);
				return true;
			case GLFW_KEY_DOWN:
				cursor_move_to_next_line(selectionMode);
				return true;
			case GLFW_KEY_LEFT:
				if (mods & GLFW_MOD_CONTROL) {
					cursor_move_to_prev_word(selectionMode);
				}
				else {
					cursor_move_to_prev_character(selectionMode);
				}
				return true;
			case GLFW_KEY_RIGHT:
				if (mods & GLFW_MOD_CONTROL) {
					cursor_move_to_next_word(selectionMode);
				}
				else {
					cursor_move_to_next_character(selectionMode);
				}
				return true;
			case GLFW_KEY_HOME:
				if (mods & GLFW_MOD_CONTROL) {
					cursor_move_to_text_start(selectionMode);
				}
				else {
					cursor_move_to_line_start(selectionMode);
				}
				break;
			case GLFW_KEY_END:
				if (mods & GLFW_MOD_CONTROL) {
					cursor_move_to_text_end(selectionMode);
				}
				else {
					cursor_move_to_line_end(selectionMode);
				}
				break;
			case GLFW_KEY_BACKSPACE:
				handle_key_backspace(mods & GLFW_MOD_CONTROL);
				break;
			case GLFW_KEY_DELETE:
				handle_key_delete(mods & GLFW_MOD_CONTROL);
				break;
			case GLFW_KEY_ENTER:
				handle_key_enter(container);
				break;
			case GLFW_KEY_X:
				if (mods & GLFW_MOD_CONTROL) {
					clipboard_cut_text();
				}
				break;
			case GLFW_KEY_C:
				if (mods & GLFW_MOD_CONTROL) {
					clipboard_copy_text();
				}
				break;
			case GLFW_KEY_V:
				if (mods & GLFW_MOD_CONTROL) {
					clipboard_paste_text();
				}
				break;
			case GLFW_KEY_A:
				if (mods & GLFW_MOD_CONTROL) {
					cursor_move_to_text_start(false);
					cursor_move_to_text_end(true);
				}
				break;
			case GLFW_KEY_TAB:
				handle_key_tab();
				break;
			default:
				break;
		}

		return true;
	}

	return false;
}

bool TextBox::handle_mouse_move(UIContainer& container, double mouseX, double mouseY) {
	if (is_focused() && m_dragSelecting) {
		cursor_move_to_mouse(mouseX - get_absolute_position()[0], mouseY - get_absolute_position()[1], true);
	}

	return false;
}

bool TextBox::handle_text_input(UIContainer&, unsigned codepoint) {
	if (is_focused() && m_editable) {
		insert_typed_character(codepoint);
		return true;
	}

	return false;
}

void TextBox::handle_focused(UIContainer&) {
	recalc_text();
}

void TextBox::handle_focus_lost(UIContainer&) {
	m_selectionStart = {Text::CursorPosition::INVALID_VALUE};
	m_dragSelecting = false;
	recalc_text();
}

void TextBox::update(float deltaTime) {
	UIObject::update(deltaTime);

	m_cursorTimer += deltaTime;

	while (m_cursorFlashIndex < 10 && m_cursorTimer >= 0.5f) {
		m_cursorTimer -= 0.5f;
		++m_cursorFlashIndex;
	}
}

void TextBox::render(UIContainer& container) {
	container.draw_text(m_layout, m_formatting, get_absolute_position()[0], get_absolute_position()[1],
			get_size()[0], get_size()[1], m_textXAlignment, m_textYAlignment, m_vertical, m_selectionStart,
			m_cursorPosition);

	// Draw Cursor
	if (is_focused() && (m_cursorFlashIndex & 1) == 0) {
		Text::Color cursorColor{0, 0, 0, 1};
		container.emit_rect(get_absolute_position()[0] + m_visualCursorInfo.x,
				get_absolute_position()[1] + m_visualCursorInfo.y, 1, m_visualCursorInfo.height, cursorColor,
				PipelineIndex::RECT);
	}
}

// Private Methods

bool TextBox::should_focused_use_rich_text() const {
	// Focused should only use rich text if the text box is not editable
	// NOTE: In a more general sense, this is only true whenever the formatting source is inline
	return m_richText && !m_editable;
}

void TextBox::cursor_move_to_next_character(bool selectionMode) {
	set_cursor_position_internal(m_cursorCtrl.next_character(m_cursorPosition), selectionMode);
}

void TextBox::cursor_move_to_prev_character(bool selectionMode) {
	set_cursor_position_internal(m_cursorCtrl.prev_character(m_cursorPosition), selectionMode);
}

void TextBox::cursor_move_to_next_word(bool selectionMode) {
	set_cursor_position_internal(m_cursorCtrl.next_word(m_cursorPosition), selectionMode);
}

void TextBox::cursor_move_to_prev_word(bool selectionMode) {
	set_cursor_position_internal(m_cursorCtrl.prev_word(m_cursorPosition), selectionMode);
}

void TextBox::cursor_move_to_next_line(bool selectionMode) {
	auto cursor = m_visualCursorInfo.lineNumber < m_layout.get_line_count() - 1
			? m_cursorCtrl.closest_in_line(m_layout, get_size()[0], m_textXAlignment,
					m_visualCursorInfo.lineNumber + 1, m_visualCursorInfo.x)
			: m_cursorPosition;
	set_cursor_position_internal(cursor, selectionMode);
}

void TextBox::cursor_move_to_prev_line(bool selectionMode) {
	auto cursor = m_visualCursorInfo.lineNumber > 0
			? m_cursorCtrl.closest_in_line(m_layout, get_size()[0], m_textXAlignment,
					m_visualCursorInfo.lineNumber - 1, m_visualCursorInfo.x)
			: m_cursorPosition;
	set_cursor_position_internal(cursor, selectionMode);
}

void TextBox::cursor_move_to_line_start(bool selectionMode) {
	set_cursor_position_internal(m_layout.get_line_start_position(m_visualCursorInfo.lineNumber), selectionMode);
}

void TextBox::cursor_move_to_line_end(bool selectionMode) {
	set_cursor_position_internal(m_layout.get_line_end_position(m_visualCursorInfo.lineNumber), selectionMode);
}

void TextBox::cursor_move_to_text_start(bool selectionMode) {
	set_cursor_position_internal({}, selectionMode);
}

void TextBox::cursor_move_to_text_end(bool selectionMode) {
	set_cursor_position_internal({static_cast<uint32_t>(m_cursorCtrl.get_text().size())}, selectionMode);
}

void TextBox::cursor_move_to_mouse(double mouseX, double mouseY, bool selectionMode) {
	auto cursor = m_cursorCtrl.closest_to_position(m_layout, get_size()[0], m_textXAlignment,
			static_cast<float>(mouseX), static_cast<float>(mouseY));
	set_cursor_position_internal(cursor, selectionMode);
}

void TextBox::set_cursor_position_internal(Text::CursorPosition pos, bool selectionMode) {
	if (selectionMode) {
		if (!m_selectionStart.is_valid()) {
			m_selectionStart = m_cursorPosition;
		}

		m_cursorPosition = pos;
	}
	else {
		m_selectionStart = {Text::CursorPosition::INVALID_VALUE};
		m_cursorPosition = pos;
	}

	m_visualCursorInfo = m_layout.calc_cursor_pixel_pos(get_size()[0], m_textXAlignment, m_cursorPosition);
	m_cursorTimer = 0.f;
	m_cursorFlashIndex = 0;
}

void TextBox::handle_key_backspace(bool ctrl) {
	if (m_selectionStart.is_valid()) {
		remove_highlighted_text();
	}
	else if (m_cursorPosition.get_position() > 0) {
		auto endPos = m_cursorPosition.get_position();

		if (ctrl) {
			cursor_move_to_prev_word(false);
		}
		else {
			cursor_move_to_prev_character(false);
		}

		remove_text(m_cursorPosition.get_position(), endPos);
	}
}

void TextBox::handle_key_delete(bool ctrl) {
	if (m_selectionStart.is_valid()) {
		remove_highlighted_text();
	}
	else if (m_cursorPosition.get_position() < m_text.size()) {
		auto startPos = m_cursorPosition;

		if (ctrl) {
			cursor_move_to_next_word(false);
		}
		else {
			cursor_move_to_next_character(false);
		}

		auto endPos = m_cursorPosition.get_position();
		m_cursorPosition = startPos;
		remove_text(startPos.get_position(), endPos);
	}
}

void TextBox::handle_key_enter(UIContainer& container) {
	if (m_multiLine) {
		remove_highlighted_text();
		insert_typed_character('\n');
	}
	else {
		container.release_focused_object();
	}
}

void TextBox::handle_key_tab() {
	insert_typed_character('\t');
}

void TextBox::clipboard_cut_text() {
	if (!m_editable) {
		return;
	}

	clipboard_copy_text();
	remove_highlighted_text();
}

void TextBox::clipboard_copy_text() {
	if (!m_selectionStart.is_valid()) {
		return;
	}

	auto startPos = m_selectionStart.get_position();
	auto endPos = m_cursorPosition.get_position();

	if (startPos == endPos) {
		return;
	}
	else if (startPos > endPos) {
		std::swap(startPos, endPos);
	}

	auto str = m_text.substr(startPos, endPos - startPos);
	glfwSetClipboardString(NULL, str.c_str());
}

void TextBox::clipboard_paste_text() {
	if (!m_editable) {
		return;
	}

	remove_highlighted_text();
	insert_text(glfwGetClipboardString(NULL), m_cursorPosition.get_position());
}

void TextBox::insert_typed_character(unsigned codepoint) {
	if (m_selectionStart.is_valid()) {
		remove_highlighted_text();
	}

	int32_t len{};
	char buffer[4]{};
	U8_APPEND_UNSAFE(buffer, len, codepoint);
	insert_text(std::string(buffer, len), m_cursorPosition.get_position());
}

void TextBox::insert_text(const std::string& text, uint32_t startIndex) {
	m_cursorPosition = {static_cast<uint32_t>(m_cursorPosition.get_position() + text.size())};

	if (startIndex < m_text.size()) {
		auto before = m_text.substr(0, startIndex);
		auto after = m_text.substr(startIndex);
		set_text(before + text + after);
	}
	else {
		set_text(m_text + text);
	}
}

void TextBox::remove_text(uint32_t startIndex, uint32_t endIndex) {
	auto before = m_text.substr(0, startIndex);
	auto after = m_text.substr(endIndex);
	set_text(before + after);
}

void TextBox::remove_highlighted_text() {
	auto start = m_selectionStart;
	auto end = m_cursorPosition;

	if (start == end || !start.is_valid()) {
		return;
	}
	else if (start.get_position() > end.get_position()) {
		std::swap(start, end);
	}

	m_cursorPosition = start;
	m_selectionStart = {Text::CursorPosition::INVALID_VALUE};
	remove_text(start.get_position(), end.get_position());
}

void TextBox::recalc_text() {
	bool richText = is_focused() ? should_focused_use_rich_text() : m_richText;

	m_visualCursorInfo = {};

	if (!m_font) {
		return;
	}

	Text::StrokeState strokeState{};
	m_formatting = richText
			? Text::parse_inline_formatting(m_text, m_contentText, m_font, m_textColor, strokeState)
			: Text::make_default_formatting_runs(m_text, m_contentText, m_font, m_textColor, strokeState);

	auto& text = richText ? m_contentText : m_text;
	m_cursorCtrl.set_text(text);

	if (text.empty()) {
		auto fontData = Text::FontRegistry::get_font_data(m_font);
		m_visualCursorInfo.height = fontData.get_ascent() - fontData.get_descent();
		return;
	}

	Text::LayoutBuilder builder;
	Text::LayoutBuildParams params{
		.textAreaWidth = m_textWrapped ? get_size()[0] : 0.f,
		.textAreaHeight = get_size()[1],
		.tabWidth = 8.f,
		.flags = m_vertical ? Text::LayoutInfoFlags::VERTICAL : Text::LayoutInfoFlags::NONE,
		.xAlignment = m_textXAlignment,
		.yAlignment = m_textYAlignment,
		.pSmallcapsRuns = &m_formatting.smallcapsRuns,
		.pSubscriptRuns = &m_formatting.subscriptRuns,
		.pSuperscriptRuns = &m_formatting.superscriptRuns,
	};
	builder.build_layout_info(m_layout, text.data(), text.size(), m_formatting.fontRuns, params);

	m_visualCursorInfo = m_layout.calc_cursor_pixel_pos(get_size()[0], m_textXAlignment, m_cursorPosition);
}

// Setters

void TextBox::set_font(Text::Font font) {
	m_font = std::move(font);
	recalc_text();
}

void TextBox::set_text(std::string text) {
	m_text = std::move(text);
	recalc_text();
}

void TextBox::set_text_x_alignment(Text::XAlignment align) {
	m_textXAlignment = align;
	recalc_text();
}

void TextBox::set_text_y_alignment(Text::YAlignment align) {
	m_textYAlignment = align;
	recalc_text();
}

void TextBox::set_text_wrapped(bool wrapped) {
	m_textWrapped = wrapped;
	recalc_text();
}

void TextBox::set_multi_line(bool multiLine) {
	m_multiLine = multiLine;
}

void TextBox::set_rich_text(bool richText) {
	m_richText = richText;
	recalc_text();
}

void TextBox::set_editable(bool editable) {
	m_editable = editable;
}

void TextBox::set_selectable(bool selectable) {
	m_selectable = selectable;
}

void TextBox::set_size(float width, float height) {
	UIObject::set_size(width, height);
	recalc_text();
}

