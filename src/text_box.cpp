#include "text_box.hpp"

#include "config_vars.hpp"
#include "font_cache.hpp"
#include "image.hpp"
#include "pipeline.hpp"
#include "text_atlas.hpp"
#include "msdf_text_atlas.hpp"
#include "text_utils.hpp"
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
static size_t g_lineNumber = 0; 

static CursorPosition apply_cursor_move(const ParagraphLayout& paragraphLayout, float textWidth,
		TextXAlignment textXAlignment, const PostLayoutCursorMove& op, CursorPosition cursor);

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
			case GLFW_KEY_UP:
				cursor_move_to_prev_line();
				return true;
			case GLFW_KEY_DOWN:
				cursor_move_to_next_line();
				return true;
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

		if (CVars::showGlyphOutlines && rect.pipeline != PipelineIndex::OUTLINE) {
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
	if (auto nextIndex = g_charBreakIter->following(m_cursorPosition.get_position());
			nextIndex != icu::BreakIterator::DONE) {
		m_cursorPosition = {static_cast<uint32_t>(nextIndex)};
	}

	recalc_text_internal(false, nullptr);
}

void TextBox::cursor_move_to_prev_character() {
	if (auto nextIndex = g_charBreakIter->preceding(m_cursorPosition.get_position());
			nextIndex != icu::BreakIterator::DONE) {
		m_cursorPosition = {static_cast<uint32_t>(nextIndex)};
	}

	recalc_text_internal(false, nullptr);
}

void TextBox::cursor_move_to_next_word() {
	bool lastWhitespace = u_isWhitespace(g_unicodeString.char32At(m_cursorPosition.get_position()));

	for (;;) {
		auto nextIndex = g_charBreakIter->following(m_cursorPosition.get_position());

		if (nextIndex == icu::BreakIterator::DONE) {
			break;
		}

		m_cursorPosition = {static_cast<uint32_t>(nextIndex)};
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
		auto nextIndex = g_charBreakIter->preceding(m_cursorPosition.get_position());

		if (nextIndex == icu::BreakIterator::DONE) {
			break;
		}

		auto c = g_unicodeString.char32At(nextIndex);

		bool whitespace = u_isWhitespace(c);

		if (whitespace && !lastWhitespace) {
			break;
		}

		if (is_line_break(c)) {
			m_cursorPosition = {static_cast<uint32_t>(nextIndex)};
			break;
		}

		m_cursorPosition = {static_cast<uint32_t>(nextIndex)};
		lastWhitespace = whitespace;
	}

	recalc_text_internal(false, nullptr);
}

void TextBox::cursor_move_to_next_line() {
	PostLayoutCursorMove op{PostLayoutCursorMoveType::LINE_BELOW};
	recalc_text_internal(false, &op);
}

void TextBox::cursor_move_to_prev_line() {
	PostLayoutCursorMove op{PostLayoutCursorMoveType::LINE_ABOVE};
	recalc_text_internal(false, &op);
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
	m_cursorPosition = {};
	recalc_text_internal(false, nullptr);
}

void TextBox::cursor_move_to_text_end() {
	m_cursorPosition = {static_cast<uint32_t>(g_unicodeString.length())};
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
	ParagraphLayout paragraphLayout{};
	build_paragraph_layout(paragraphLayout, textInfo.str.getBuffer(), textInfo.str.length(), textInfo.fontRuns,
			m_size[0], m_size[1], m_textYAlignment, ParagraphLayoutFlags::NONE);

	if (postLayoutOp) {
		m_cursorPosition = apply_cursor_move(paragraphLayout, m_size[0], m_textXAlignment,
				*reinterpret_cast<const PostLayoutCursorMove*>(postLayoutOp), m_cursorPosition);
	}

	auto cursorPixelInfo = paragraphLayout.calc_cursor_pixel_pos(m_size[0], m_textXAlignment, m_cursorPosition);
	g_cursorPixelX = cursorPixelInfo.x;
	g_cursorPixelY = cursorPixelInfo.y;
	g_cursorHeight = cursorPixelInfo.height;
	g_lineNumber = cursorPixelInfo.lineNumber;

	// Add Stroke Glyphs
	paragraphLayout.for_each_glyph(m_size[0], m_textXAlignment, [&](auto glyphID, auto charIndex,
			auto* position, auto& font, auto lineX, auto lineY) {
		auto stroke = textInfo.strokeRuns.get_value(charIndex);

		if (stroke.color.a > 0.f) {
			auto pX = position[0];
			auto pY = position[1];

			float offset[2]{};
			float texCoordExtents[4]{};
			float glyphSize[2]{};
			bool strokeHasColor{};
			auto* pGlyphImage = CVars::useMSDF ? g_msdfTextAtlas->get_stroke_info(font, glyphID, stroke.thickness,
					stroke.joins, texCoordExtents, glyphSize, offset, strokeHasColor)
					: g_textAtlas->get_stroke_info(font, glyphID, stroke.thickness, stroke.joins,
					texCoordExtents, glyphSize, offset, strokeHasColor);

			m_textRects.push_back({
				.x = lineX + pX + offset[0],
				.y = paragraphLayout.textStartY + lineY + pY + offset[1],
				.width = glyphSize[0],
				.height = glyphSize[1],
				.texCoords = {texCoordExtents[0], texCoordExtents[1],
						texCoordExtents[2], texCoordExtents[3]},
				.texture = pGlyphImage,
				.color = stroke.color,
				.pipeline = CVars::useMSDF ? PipelineIndex::MSDF : PipelineIndex::RECT,
			});
		}
	});

	// Add Main Glyphs
	paragraphLayout.for_each_glyph(m_size[0], m_textXAlignment, [&](auto glyphID, auto charIndex,
			auto* position, auto& font, auto lineX, auto lineY) {
		auto pX = position[0];
		auto pY = position[1];

		float offset[2]{};
		float texCoordExtents[4]{};
		float glyphSize[2]{};
		bool glyphHasColor{};

		auto* pGlyphImage = CVars::useMSDF ? g_msdfTextAtlas->get_glyph_info(font, glyphID, texCoordExtents,
				glyphSize, offset, glyphHasColor)
				: g_textAtlas->get_glyph_info(font, glyphID, texCoordExtents, glyphSize, offset,
				glyphHasColor);
		auto textColor = glyphHasColor ? Color{1.f, 1.f, 1.f, 1.f} : textInfo.colorRuns.get_value(charIndex);

		if (textInfo.strikethroughRuns.get_value(charIndex)) {
			auto height = font.get_strikethrough_thickness() + 0.5f;
			m_textRects.push_back({
				.x = lineX + pX + offset[0],
				.y = paragraphLayout.textStartY + lineY + pY + font.get_strikethrough_position(),
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
				.y = paragraphLayout.textStartY + lineY + pY + font.get_underline_position(),
				.width = glyphSize[0],
				.height = height,
				.texture = g_textAtlas->get_default_texture(),
				.color = textColor,
				.pipeline = PipelineIndex::RECT,
			});
		}

		m_textRects.push_back({
			.x = lineX + pX + offset[0],
			.y = paragraphLayout.textStartY + lineY + pY + offset[1],
			.width = glyphSize[0],
			.height = glyphSize[1],
			.texCoords = {texCoordExtents[0], texCoordExtents[1], texCoordExtents[2], texCoordExtents[3]},
			.texture = pGlyphImage,
			.color = std::move(textColor),
			.pipeline = CVars::useMSDF ? PipelineIndex::MSDF : PipelineIndex::RECT,
		});
	});

	// Debug render run outlines
	if (CVars::showRunOutlines) {
		paragraphLayout.for_each_run(m_size[0], m_textXAlignment, [&](auto lineIndex, auto runIndex, auto lineX,
				auto lineY) {
			auto* positions = paragraphLayout.get_run_positions(runIndex);
			auto minBound = positions[0];
			auto maxBound = positions[2 * paragraphLayout.get_run_glyph_count(runIndex)]; 

			m_textRects.push_back({
				.x = lineX + minBound,
				.y = lineY - paragraphLayout.lines[lineIndex].ascent,
				.width = maxBound - minBound,
				.height = paragraphLayout.get_line_height(lineIndex),
				.texture = g_textAtlas->get_default_texture(),
				.color = {0, 0.5, 0, 1},
				.pipeline = PipelineIndex::OUTLINE,
			});
		});
	}

	// Debug render glyph boundaries
	if (CVars::showGlyphBoundaries) {
		paragraphLayout.for_each_run(m_size[0], m_textXAlignment, [&](auto lineIndex, auto runIndex, auto lineX,
				auto lineY) {
			auto* positions = paragraphLayout.get_run_positions(runIndex);

			for (le_int32 i = 0; i <= paragraphLayout.get_run_glyph_count(runIndex); ++i) {
				m_textRects.push_back({
					.x = lineX + positions[2 * i],
					.y = lineY - paragraphLayout.lines[lineIndex].ascent,
					.width = 0.5f,
					.height = paragraphLayout.get_line_height(lineIndex),
					.texture = g_textAtlas->get_default_texture(),
					.color = {0, 0.5, 0, 1},
					.pipeline = PipelineIndex::OUTLINE,
				});
			}
		});
	}
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

static CursorPosition apply_cursor_move(const ParagraphLayout& paragraphLayout, float textWidth,
		TextXAlignment textXAlignment, const PostLayoutCursorMove& op, CursorPosition cursor) {
	switch (op.type) {
		case PostLayoutCursorMoveType::LINE_START:
			return paragraphLayout.get_line_start_position(g_lineNumber);
		case PostLayoutCursorMoveType::LINE_END:
			return paragraphLayout.get_line_end_position(g_lineNumber);
		case PostLayoutCursorMoveType::LINE_ABOVE:
			return g_lineNumber > 0
					? paragraphLayout.find_closest_cursor_position(textWidth, textXAlignment, *g_charBreakIter,
							g_lineNumber - 1, g_cursorPixelX)
					: cursor;
		case PostLayoutCursorMoveType::LINE_BELOW:
			return g_lineNumber < paragraphLayout.lines.size() - 1
					? paragraphLayout.find_closest_cursor_position(textWidth, textXAlignment, *g_charBreakIter,
							g_lineNumber + 1, g_cursorPixelX)
					: cursor;
		default:
			break;
	}

	return cursor;
}

static bool is_line_break(UChar32 c) {
	return c == CH_LF || c == CH_CR || c == CH_LSEP || c == CH_PSEP;
}

