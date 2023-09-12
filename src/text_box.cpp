#include "text_box.hpp"

#include "font_cache.hpp"
#include "image.hpp"
#include "pipeline.hpp"
#include "text_atlas.hpp"
#include "msdf_text_atlas.hpp"
#include "paragraph_layout.hpp"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <unicode/brkiter.h>

namespace {

enum class PostLayoutCursorMoveType {
	LINE_START,
	LINE_END,
	LINE_ABOVE,
	LINE_BELOW,
	MOUSE_POSITION
};

struct PostLayoutCursorMove {
	PostLayoutCursorMoveType type;
};

struct CursorToMouse : public PostLayoutCursorMove {
	double mouseX;
	double mouseY;
};

}

static constexpr const bool g_useMSDF = false;
static constexpr const bool g_showGlyphOutlines = false;

static constexpr const UChar32 CH_LF = 0x000A;
static constexpr const UChar32 CH_CR = 0x000D;
static constexpr const UChar32 CH_LSEP = 0x2028;
static constexpr const UChar32 CH_PSEP = 0x2029;

static icu::UnicodeString g_unicodeString;
static icu::BreakIterator* g_charBreakIter = nullptr;
static TextBox* g_focusedTextBox = nullptr;
static float g_cursorPixelX = 0.f;
static float g_cursorPixelY = 0.f;
static float g_cursorHeight = 0.f;

static int32_t apply_cursor_move(const Text::LayoutInfo& layoutInfo, const PostLayoutCursorMove& op,
		int32_t cursorPos);
static void calc_cursor_pixel_pos(const Text::LayoutInfo& layoutInfo, float textWidth,
		TextXAlignment textXAlignment, float yStart, int32_t cursorPosition, float& outX, float& outY,
		float& outHeight);

static bool is_line_break(UChar32 c);

TextBox* TextBox::get_focused_text_box() {
	return g_focusedTextBox;
}

TextBox::~TextBox() {
	release_focus();
}

bool TextBox::handle_mouse_button(int button, double mouseX, double mouseY) {
	bool mouseInside = is_mouse_inside(mouseX, mouseY);

	if (g_focusedTextBox == this) {
		if (is_mouse_inside(mouseX, mouseY)) {
			// FIXME: click inside focused text box - move cursor or something
		}
		else {
			release_focus();
		}
	}
	else {
		capture_focus();
	}

	return mouseInside;
}

bool TextBox::handle_key_press(int key, int action, int mods) {
	if (action == GLFW_RELEASE) {
		return false;
	}

	if (is_focused()) {
		switch (key) {
			case GLFW_KEY_LEFT:
				if (mods & GLFW_MOD_CONTROL) {
					cursor_move_to_prev_word();
				}
				else {
					cursor_move_to_prev_character();
				}
				return true;
			case GLFW_KEY_RIGHT:
				if (mods & GLFW_MOD_CONTROL) {
					cursor_move_to_next_word();
				}
				else {
					cursor_move_to_next_character();
				}
				return true;
			case GLFW_KEY_HOME:
				if (mods & GLFW_MOD_CONTROL) {
					cursor_move_to_text_start();
				}
				else {
					cursor_move_to_line_start();
				}
				break;
			case GLFW_KEY_END:
				if (mods & GLFW_MOD_CONTROL) {
					cursor_move_to_text_end();
				}
				else {
					cursor_move_to_line_end();
				}
				break;
			default:
				break;
		}

		return true;
	}

	return false;
}

bool TextBox::handle_text_input(unsigned codepoint) {
	return g_focusedTextBox == this;
}

void TextBox::capture_focus() {
	if (g_focusedTextBox == this) {
		return;
	}
	else if (g_focusedTextBox) {
		g_focusedTextBox->release_focus();
	}

	g_focusedTextBox = this;
	g_unicodeString = icu::UnicodeString::fromUTF8(m_text);

	icu::Locale loc("en_US");
	UErrorCode errc{};

	g_charBreakIter = icu::BreakIterator::createCharacterInstance(loc, errc);
	g_charBreakIter->setText(g_unicodeString);

	recalc_text_internal(false, nullptr);
}

void TextBox::release_focus() {
	if (g_focusedTextBox != this) {
		return;
	}

	delete g_charBreakIter;
	g_unicodeString = icu::UnicodeString{};
	g_focusedTextBox = nullptr;

	recalc_text();
}

void TextBox::render(const float* invScreenSize) {
	PipelineIndex pipelineIndex{PipelineIndex::INVALID};
	Pipeline* pPipeline = nullptr;

	for (auto& rect : m_textRects) {
		if (!rect.texture) {
			continue;
		}

		if (rect.pipeline != pipelineIndex) {
			pipelineIndex = rect.pipeline;
			pPipeline = &g_pipelines[static_cast<size_t>(pipelineIndex)];
			pPipeline->bind();
			pPipeline->set_uniform_float2(0, invScreenSize);
		}

		rect.texture->bind();
		float extents[] = {m_position[0] + rect.x, m_position[1] + rect.y, rect.width, rect.height}; 
		pPipeline->set_uniform_float4(1, extents);
		pPipeline->set_uniform_float4(2, rect.texCoords);
		pPipeline->set_uniform_float4(3, reinterpret_cast<const float*>(&rect.color));
		pPipeline->draw();

		if (g_showGlyphOutlines && rect.pipeline != PipelineIndex::OUTLINE) {
			if (pipelineIndex != PipelineIndex::OUTLINE) {
				pipelineIndex = PipelineIndex::OUTLINE;
				pPipeline = &g_pipelines[static_cast<size_t>(pipelineIndex)];
				pPipeline->bind();
				pPipeline->set_uniform_float2(0, invScreenSize);
			}

			Color outlineColor{0.f, 0.5f, 0.f, 1.f};
			pPipeline->set_uniform_float4(1, extents);
			pPipeline->set_uniform_float4(2, rect.texCoords);
			pPipeline->set_uniform_float4(3, reinterpret_cast<const float*>(&outlineColor));
			pPipeline->draw();
		}
	}

	// Draw Cursor
	if (is_focused()) {
		float cursorExtents[] = {m_position[0] + g_cursorPixelX, m_position[1] + g_cursorPixelY, 1,
				g_cursorHeight};
		Color cursorColor{0, 0, 0, 1};

		pPipeline = &g_pipelines[static_cast<size_t>(PipelineIndex::RECT)];
		pPipeline->bind();
		pPipeline->set_uniform_float2(0, invScreenSize);
		pPipeline->set_uniform_float4(1, cursorExtents);
		pPipeline->set_uniform_float4(3, reinterpret_cast<const float*>(&cursorColor));
		g_textAtlas->get_default_texture()->bind();
		pPipeline->draw();
	}
}

bool TextBox::is_mouse_inside(double mouseX, double mouseY) const {
	return mouseX >= m_position[0] && mouseY >= m_position[1] && mouseX - m_position[0] <= m_size[0]
			&& mouseY - m_position[1] <= m_size[1];
}

bool TextBox::is_focused() const {
	return g_focusedTextBox == this;
}

// Private Methods

void TextBox::cursor_move_to_next_character() {
	if (auto nextIndex = g_charBreakIter->following(m_cursorPosition); nextIndex != icu::BreakIterator::DONE) {
		m_cursorPosition = nextIndex;
	}

	recalc_text_internal(false, nullptr);
}

void TextBox::cursor_move_to_prev_character() {
	if (auto nextIndex = g_charBreakIter->preceding(m_cursorPosition); nextIndex != icu::BreakIterator::DONE) {
		m_cursorPosition = nextIndex;
	}

	recalc_text_internal(false, nullptr);
}

void TextBox::cursor_move_to_next_word() {
	bool lastWhitespace = u_isWhitespace(g_unicodeString.char32At(m_cursorPosition));

	for (;;) {
		auto nextIndex = g_charBreakIter->following(m_cursorPosition);

		if (nextIndex == icu::BreakIterator::DONE) {
			break;
		}

		m_cursorPosition = nextIndex;
		auto c = g_unicodeString.char32At(nextIndex);
		bool whitespace = u_isWhitespace(c);

		if (!whitespace && lastWhitespace || is_line_break(c)) {
			break;
		}

		lastWhitespace = whitespace;
	}

	recalc_text_internal(false, nullptr);
}

void TextBox::cursor_move_to_prev_word() {
	bool lastWhitespace = true;

	for (;;) {
		auto nextIndex = g_charBreakIter->preceding(m_cursorPosition);

		if (nextIndex == icu::BreakIterator::DONE) {
			break;
		}

		auto c = g_unicodeString.char32At(nextIndex);

		bool whitespace = u_isWhitespace(c);

		if (whitespace && !lastWhitespace) {
			break;
		}

		if (is_line_break(c)) {
			m_cursorPosition = nextIndex;
			break;
		}

		m_cursorPosition = nextIndex;
		lastWhitespace = whitespace;
	}

	recalc_text_internal(false, nullptr);
}

void TextBox::cursor_move_to_line_start() {
	PostLayoutCursorMove op{PostLayoutCursorMoveType::LINE_START};
	recalc_text_internal(false, &op);
}

void TextBox::cursor_move_to_line_end() {
	PostLayoutCursorMove op{PostLayoutCursorMoveType::LINE_END};
	recalc_text_internal(false, &op);
}

void TextBox::cursor_move_to_text_start() {
	m_cursorPosition = 0;
	recalc_text_internal(false, nullptr);
}

void TextBox::cursor_move_to_text_end() {
	m_cursorPosition = g_unicodeString.length();
	recalc_text_internal(false, nullptr);
}

void TextBox::recalc_text() {
	recalc_text_internal(m_richText, nullptr);
}

void TextBox::recalc_text_internal(bool richText, const void* postLayoutOp) {
	m_textRects.clear();

	if (!m_font) {
		return;
	}

	RichText::StrokeState strokeState{};
	auto runs = richText ? RichText::parse(m_text, m_contentText, m_font, m_textColor, strokeState)
			: RichText::make_default_runs(m_text, m_contentText, m_font, m_textColor, strokeState);

	if (m_contentText.empty()) {
		return;
	}

	create_text_rects(runs, postLayoutOp);
}

void TextBox::create_text_rects(RichText::Result& textInfo, const void* postLayoutOp) {
	Text::LayoutInfo layoutInfo{};
	Text::build_line_layout_info(textInfo, m_size[0], layoutInfo);
	auto textHeight = Text::get_text_height(layoutInfo);
	auto yStart = static_cast<float>(m_textYAlignment) * (m_size[1] - textHeight) * 0.5f;

	if (postLayoutOp) {
		m_cursorPosition = apply_cursor_move(layoutInfo,
				*reinterpret_cast<const PostLayoutCursorMove*>(postLayoutOp), m_cursorPosition);
	}

	calc_cursor_pixel_pos(layoutInfo, m_size[0], m_textXAlignment, yStart, m_cursorPosition, g_cursorPixelX,
			g_cursorPixelY, g_cursorHeight);

	// Add Stroke Glyphs
	Text::for_each_glyph(layoutInfo, m_size[0], m_textXAlignment, [&](auto glyphID, auto charIndex,
			auto* position, auto& font, auto lineX, auto lineY) {
		auto stroke = textInfo.strokeRuns.get_value(charIndex);

		if (stroke.color.a > 0.f) {
			auto pX = position[0];
			auto pY = position[1];

			float offset[2]{};
			float texCoordExtents[4]{};
			float glyphSize[2]{};
			bool strokeHasColor{};
			auto* pGlyphImage = g_useMSDF ? g_msdfTextAtlas->get_stroke_info(font, glyphID, stroke.thickness,
					stroke.joins, texCoordExtents, glyphSize, offset, strokeHasColor)
					: g_textAtlas->get_stroke_info(font, glyphID, stroke.thickness, stroke.joins,
					texCoordExtents, glyphSize, offset, strokeHasColor);

			m_textRects.push_back({
				.x = lineX + pX + offset[0],
				.y = yStart + lineY + pY + offset[1],
				.width = glyphSize[0],
				.height = glyphSize[1],
				.texCoords = {texCoordExtents[0], texCoordExtents[1],
						texCoordExtents[2], texCoordExtents[3]},
				.texture = pGlyphImage,
				.color = stroke.color,
				.pipeline = g_useMSDF ? PipelineIndex::MSDF : PipelineIndex::RECT,
			});
		}
	});

	// Add cursor
	Text::for_each_line(layoutInfo, m_size[0], m_textXAlignment, [&](auto lineNumber, auto* pLine,
			auto charOffset, auto lineX, auto lineY) {
		float offset = 0.f;
		bool found = false;

		auto lineStart = Text::get_line_char_start_index(pLine, charOffset);
		auto lineEnd = Text::get_line_char_end_index(pLine, charOffset);

		if (m_cursorPosition >= lineStart && m_cursorPosition <= lineEnd) {
			offset = Text::get_cursor_offset_in_line(pLine, m_cursorPosition - charOffset);
			found = true;
		}
		else if (m_cursorPosition >= lineEnd && lineNumber < layoutInfo.lines.size() - 1) {
			auto* pNextLine = layoutInfo.lines[lineNumber + 1].get();
			auto nextOffset = layoutInfo.offsetRunsByLine.get_value(static_cast<int32_t>(lineNumber + 1));

			if (m_cursorPosition < Text::get_line_char_start_index(pNextLine, nextOffset)) {
				offset = Text::get_line_end_position(pLine);
				found = true;
			}
		}
		else if (m_cursorPosition >= lineEnd) {
			offset = Text::get_line_end_position(pLine);
			found = true;
		}

		if (found) {
			m_textRects.push_back({
				.x = lineX + offset,
				.y = yStart + lineY - layoutInfo.lineY,
				.width = 1,
				.height = layoutInfo.lineHeight,
				.texture = g_textAtlas->get_default_texture(),
				.color = {0, 0, 0, 1},
				.pipeline = PipelineIndex::RECT,
			});
		}
	});

	// Add Main Glyphs
	Text::for_each_glyph(layoutInfo, m_size[0], m_textXAlignment, [&](auto glyphID, auto charIndex,
			auto* position, auto& font, auto lineX, auto lineY) {
		auto pX = position[0];
		auto pY = position[1];

		float offset[2]{};
		float texCoordExtents[4]{};
		float glyphSize[2]{};
		bool glyphHasColor{};

		auto* pGlyphImage = g_useMSDF ? g_msdfTextAtlas->get_glyph_info(font, glyphID, texCoordExtents,
				glyphSize, offset, glyphHasColor)
				: g_textAtlas->get_glyph_info(font, glyphID, texCoordExtents, glyphSize, offset,
				glyphHasColor);
		auto textColor = glyphHasColor ? Color{1.f, 1.f, 1.f, 1.f} : textInfo.colorRuns.get_value(charIndex);

		if (textInfo.strikethroughRuns.get_value(charIndex)) {
			auto height = font.get_strikethrough_thickness() + 0.5f;
			m_textRects.push_back({
				.x = lineX + pX + offset[0],
				.y = yStart + lineY + pY + font.get_strikethrough_position(),
				.width = glyphSize[0],
				.height = height,
				.texture = g_textAtlas->get_default_texture(),
				.color = textColor,
				.pipeline = PipelineIndex::RECT,
			});
		}

		if (textInfo.underlineRuns.get_value(charIndex)) {
			auto height = font.get_underline_thickness() + 0.5f;
			m_textRects.push_back({
				.x = lineX + pX + offset[0],
				.y = yStart + lineY + pY + font.get_underline_position(),
				.width = glyphSize[0],
				.height = height,
				.texture = g_textAtlas->get_default_texture(),
				.color = textColor,
				.pipeline = PipelineIndex::RECT,
			});
		}

		m_textRects.push_back({
			.x = lineX + pX + offset[0],
			.y = yStart + lineY + pY + offset[1],
			.width = glyphSize[0],
			.height = glyphSize[1],
			.texCoords = {texCoordExtents[0], texCoordExtents[1], texCoordExtents[2], texCoordExtents[3]},
			.texture = pGlyphImage,
			.color = std::move(textColor),
			.pipeline = g_useMSDF ? PipelineIndex::MSDF : PipelineIndex::RECT,
		});
	});
}

// Setters

void TextBox::set_font(MultiScriptFont font) {
	m_font = std::move(font);
	recalc_text();
}

void TextBox::set_text(std::string text) {
	m_text = std::move(text);
	recalc_text();
}

void TextBox::set_position(float x, float y) {
	m_position[0] = x;
	m_position[1] = y;
	recalc_text();
}

void TextBox::set_size(float width, float height) {
	m_size[0] = width;
	m_size[1] = height;
	recalc_text();
}

void TextBox::set_text_x_alignment(TextXAlignment align) {
	m_textXAlignment = align;
	recalc_text();
}

void TextBox::set_text_y_alignment(TextYAlignment align) {
	m_textYAlignment = align;
	recalc_text();
}

void TextBox::set_text_wrapped(bool wrapped) {
	m_textWrapped = wrapped;
	recalc_text();
}

void TextBox::set_rich_text(bool richText) {
	m_richText = richText;
	recalc_text();
}

// Static Functions

static int32_t apply_cursor_move(const Text::LayoutInfo& layoutInfo, const PostLayoutCursorMove& op,
		int32_t cursorPos) {
	switch (op.type) {
		case PostLayoutCursorMoveType::LINE_START:
			return Text::find_line_start_containing_index(layoutInfo, cursorPos);
		case PostLayoutCursorMoveType::LINE_END:
			return Text::find_line_end_containing_index(layoutInfo, cursorPos, g_unicodeString.length(),
					*g_charBreakIter);
		default:
			break;
	}

	return cursorPos;
}

static void calc_cursor_pixel_pos(const Text::LayoutInfo& layoutInfo, float textWidth,
		TextXAlignment textXAlignment, float yStart, int32_t cursorPosition, float& outX, float& outY,
		float& outHeight) {
	Text::for_each_line(layoutInfo, textWidth, textXAlignment, [&](auto lineNumber, auto* pLine,
			auto charOffset, auto lineX, auto lineY) {
		float offset = 0.f;
		bool found = false;

		auto lineStart = Text::get_line_char_start_index(pLine, charOffset);
		auto lineEnd = Text::get_line_char_end_index(pLine, charOffset);

		if (cursorPosition >= lineStart && cursorPosition <= lineEnd) {
			offset = Text::get_cursor_offset_in_line(pLine, cursorPosition - charOffset);
			found = true;
		}
		else if (cursorPosition >= lineEnd && lineNumber < layoutInfo.lines.size() - 1) {
			auto* pNextLine = layoutInfo.lines[lineNumber + 1].get();
			auto nextOffset = layoutInfo.offsetRunsByLine.get_value(static_cast<int32_t>(lineNumber + 1));

			if (cursorPosition < Text::get_line_char_start_index(pNextLine, nextOffset)) {
				offset = Text::get_line_end_position(pLine);
				found = true;
			}
		}
		else if (cursorPosition >= lineEnd) {
			offset = Text::get_line_end_position(pLine);
			found = true;
		}

		if (found) {
			outX = lineX + offset;
			outY = yStart + lineY - layoutInfo.lineY;
			outHeight = layoutInfo.lineHeight;
		}
	});
}

static bool is_line_break(UChar32 c) {
	return c == CH_LF || c == CH_CR || c == CH_LSEP || c == CH_PSEP;
}

