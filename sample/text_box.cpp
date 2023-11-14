#include "text_box.hpp"

#include "config_vars.hpp"
#include "font_cache.hpp"
#include "image.hpp"
#include "pipeline.hpp"
#include "text_atlas.hpp"
#include "msdf_text_atlas.hpp"
#include "formatting.hpp"
#include "formatting_iterator.hpp"
#include "paragraph_layout.hpp"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <unicode/utext.h>
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
	bool selectionMode;
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

bool TextBox::handle_mouse_button(int button, int action, int mods, double mouseX, double mouseY) {
	if (action == GLFW_RELEASE) {
		return false;
	}

	bool mouseInside = is_mouse_inside(mouseX, mouseY);

	if (g_focusedTextBox == this) {
		if (is_mouse_inside(mouseX, mouseY)) {
			cursor_move_to_mouse(mouseX - m_position[0], mouseY - m_position[1], mods & GLFW_MOD_SHIFT);
		}
		else {
			release_focus();
		}
	}
	else {
		capture_focus();
		cursor_move_to_mouse(mouseX - m_position[0], mouseY - m_position[1], mods & GLFW_MOD_SHIFT);
	}

	return mouseInside;
}

bool TextBox::handle_key_press(int key, int action, int mods) {
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

	UErrorCode errc{};
	g_charBreakIter = icu::BreakIterator::createCharacterInstance(icu::Locale::getDefault(), errc);
	UText uText UTEXT_INITIALIZER;
	utext_openUTF8(&uText, m_text.data(), m_text.size(), &errc);
	g_charBreakIter->setText(&uText, errc);

	recalc_text_internal(false, nullptr);
}

void TextBox::release_focus() {
	if (g_focusedTextBox != this) {
		return;
	}

	delete g_charBreakIter;
	g_focusedTextBox = nullptr;

	m_selectionStart = {CursorPosition::INVALID_VALUE};

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

void TextBox::cursor_move_to_next_character(bool selectionMode) {
	if (auto nextIndex = g_charBreakIter->following(m_cursorPosition.get_position());
			nextIndex != icu::BreakIterator::DONE) {
		set_cursor_position_internal({static_cast<uint32_t>(nextIndex)}, selectionMode);
	}

	recalc_text_internal(false, nullptr);
}

void TextBox::cursor_move_to_prev_character(bool selectionMode) {
	if (auto nextIndex = g_charBreakIter->preceding(m_cursorPosition.get_position());
			nextIndex != icu::BreakIterator::DONE) {
		set_cursor_position_internal({static_cast<uint32_t>(nextIndex)}, selectionMode);
	}

	recalc_text_internal(false, nullptr);
}

void TextBox::cursor_move_to_next_word(bool selectionMode) {
	UChar32 c;
	U8_GET((const uint8_t*)m_text.data(), 0, m_cursorPosition.get_position(), m_text.size(), c);
	bool lastWhitespace = u_isWhitespace(c);

	for (;;) {
		auto nextIndex = g_charBreakIter->following(m_cursorPosition.get_position());

		if (nextIndex == icu::BreakIterator::DONE) {
			break;
		}

		set_cursor_position_internal({static_cast<uint32_t>(nextIndex)}, selectionMode);
		U8_GET((const uint8_t*)m_text.data(), 0, nextIndex, m_text.size(), c);
		bool whitespace = u_isWhitespace(c);

		if (!whitespace && lastWhitespace || is_line_break(c)) {
			break;
		}

		lastWhitespace = whitespace;
	}

	recalc_text_internal(false, nullptr);
}

void TextBox::cursor_move_to_prev_word(bool selectionMode) {
	UChar32 c;
	bool lastWhitespace = true;

	for (;;) {
		auto nextIndex = g_charBreakIter->preceding(m_cursorPosition.get_position());

		if (nextIndex == icu::BreakIterator::DONE) {
			break;
		}

		U8_GET((const uint8_t*)m_text.data(), 0, nextIndex, m_text.size(), c);

		bool whitespace = u_isWhitespace(c);

		if (whitespace && !lastWhitespace) {
			break;
		}

		if (is_line_break(c)) {
			set_cursor_position_internal({static_cast<uint32_t>(nextIndex)}, selectionMode);
			break;
		}

		set_cursor_position_internal({static_cast<uint32_t>(nextIndex)}, selectionMode);
		lastWhitespace = whitespace;
	}

	recalc_text_internal(false, nullptr);
}

void TextBox::cursor_move_to_next_line(bool selectionMode) {
	PostLayoutCursorMove op{PostLayoutCursorMoveType::LINE_BELOW, selectionMode};
	recalc_text_internal(false, &op);
}

void TextBox::cursor_move_to_prev_line(bool selectionMode) {
	PostLayoutCursorMove op{PostLayoutCursorMoveType::LINE_ABOVE, selectionMode};
	recalc_text_internal(false, &op);
}

void TextBox::cursor_move_to_line_start(bool selectionMode) {
	PostLayoutCursorMove op{PostLayoutCursorMoveType::LINE_START, selectionMode};
	recalc_text_internal(false, &op);
}

void TextBox::cursor_move_to_line_end(bool selectionMode) {
	PostLayoutCursorMove op{PostLayoutCursorMoveType::LINE_END, selectionMode};
	recalc_text_internal(false, &op);
}

void TextBox::cursor_move_to_text_start(bool selectionMode) {
	set_cursor_position_internal({}, selectionMode);
	recalc_text_internal(false, nullptr);
}

void TextBox::cursor_move_to_text_end(bool selectionMode) {
	set_cursor_position_internal({static_cast<uint32_t>(m_text.size())}, selectionMode);
	recalc_text_internal(false, nullptr);
}

void TextBox::cursor_move_to_mouse(double mouseX, double mouseY, bool selectionMode) {
	CursorToMouse op{
		.mouseX = mouseX,
		.mouseY = mouseY,
	};
	op.type = PostLayoutCursorMoveType::MOUSE_POSITION;
	op.selectionMode = selectionMode;
	recalc_text_internal(false, &op);
}

void TextBox::set_cursor_position_internal(CursorPosition pos, bool selectionMode) {
	if (selectionMode) {
		if (!m_selectionStart.is_valid()) {
			m_selectionStart = m_cursorPosition;
		}

		m_cursorPosition = pos;
	}
	else {
		m_selectionStart = {CursorPosition::INVALID_VALUE};
		m_cursorPosition = pos;
	}
}

void TextBox::recalc_text() {
	recalc_text_internal(m_richText, nullptr);
}

void TextBox::recalc_text_internal(bool richText, const void* postLayoutOp) {
	m_textRects.clear();

	if (!m_font) {
		return;
	}

	Text::StrokeState strokeState{};
	auto runs = richText ? Text::parse_inline_formatting(m_text, m_contentText, m_font, m_textColor, strokeState)
			: Text::make_default_formatting_runs(m_text, m_contentText, m_font, m_textColor, strokeState);

	if (m_contentText.empty()) {
		return;
	}

	create_text_rects(runs, richText ? m_contentText : m_text, postLayoutOp);
}

void TextBox::create_text_rects(Text::FormattingRuns& textInfo, const std::string& text, \
		const void* postLayoutOp) {
	ParagraphLayout paragraphLayout{};
	build_paragraph_layout_utf8(paragraphLayout, text.data(), text.size(), textInfo.fontRuns, m_size[0],
			m_size[1], m_textYAlignment, ParagraphLayoutFlags::NONE);

	if (postLayoutOp) {
		set_cursor_position_internal(apply_cursor_move(paragraphLayout, m_size[0], m_textXAlignment,
				*reinterpret_cast<const PostLayoutCursorMove*>(postLayoutOp), m_cursorPosition),
				reinterpret_cast<const PostLayoutCursorMove*>(postLayoutOp)->selectionMode);
	}

	auto cursorPixelInfo = paragraphLayout.calc_cursor_pixel_pos(m_size[0], m_textXAlignment, m_cursorPosition);
	g_cursorPixelX = cursorPixelInfo.x;
	g_cursorPixelY = cursorPixelInfo.y;
	g_cursorHeight = cursorPixelInfo.height;
	g_lineNumber = cursorPixelInfo.lineNumber;

	// Add highlight ranges
	if (m_selectionStart.is_valid()) {
		uint32_t selectionStart = m_selectionStart.get_position();
		uint32_t selectionEnd = m_cursorPosition.get_position();

		if (selectionStart > selectionEnd) {
			std::swap(selectionStart, selectionEnd);
		}

		paragraphLayout.for_each_run(m_size[0], m_textXAlignment, [&](auto lineIndex, auto runIndex, auto lineX,
				auto lineY) {
			auto charStartIndex = paragraphLayout.visualRuns[runIndex].charStartIndex;
			auto charEndIndex = paragraphLayout.visualRuns[runIndex].charEndIndex;

			if (charStartIndex < selectionEnd && charEndIndex > selectionStart) {
				auto minPos = paragraphLayout.get_glyph_offset_in_run(runIndex,
						std::min(selectionStart, charEndIndex));
				auto maxPos = paragraphLayout.get_glyph_offset_in_run(runIndex,
						std::min(selectionEnd, charEndIndex));
				if (paragraphLayout.visualRuns[runIndex].rightToLeft) {
					std::swap(minPos, maxPos);
				}

				emit_rect(lineX + minPos, paragraphLayout.textStartY + lineY
						- paragraphLayout.lines[lineIndex].ascent, maxPos - minPos,
						paragraphLayout.get_line_height(lineIndex), Color::from_rgb(0, 120, 215),
						PipelineIndex::RECT);
			}
		});
	}

	uint32_t glyphIndex{};
	uint32_t glyphPosIndex{};
	float strikethroughStartPos{};
	float underlineStartPos{};
	paragraphLayout.for_each_run(m_size[0], m_textXAlignment, [&](auto lineIndex, auto runIndex, auto lineX,
			auto lineY) {
		auto& run = paragraphLayout.visualRuns[runIndex];
		auto& font = *run.pFont;

		Text::FormattingIterator iter(textInfo, run.rightToLeft ? run.charEndIndex : run.charStartIndex);
		underlineStartPos = paragraphLayout.glyphPositions[glyphPosIndex];	

		for (; glyphIndex < run.glyphEndIndex; ++glyphIndex, glyphPosIndex += 2) {
			auto pX = paragraphLayout.glyphPositions[glyphPosIndex];
			auto pY = paragraphLayout.glyphPositions[glyphPosIndex + 1];
			auto glyphID = paragraphLayout.glyphs[glyphIndex];
			auto event = iter.advance_to(paragraphLayout.charIndices[glyphIndex]);
			auto stroke = iter.get_stroke_state();

			float offset[2]{};
			float texCoordExtents[4]{};
			float glyphSize[2]{};
			bool glyphHasColor{};

			// Stroke
			if (stroke.color.a > 0.f) {
				float offset[2]{};
				float texCoordExtents[4]{};
				float glyphSize[2]{};
				bool strokeHasColor{};
				auto* pGlyphImage = CVars::useMSDF ? g_msdfTextAtlas->get_stroke_info(font, glyphID,
								stroke.thickness, stroke.joins, texCoordExtents, glyphSize, offset,
								strokeHasColor)
						: g_textAtlas->get_stroke_info(font, glyphID, stroke.thickness, stroke.joins,
								texCoordExtents, glyphSize, offset, strokeHasColor);

				emit_rect(lineX + pX + offset[0], paragraphLayout.textStartY + lineY + pY + offset[1],
						glyphSize[0], glyphSize[1], texCoordExtents, pGlyphImage, stroke.color,
						CVars::useMSDF ? PipelineIndex::MSDF : PipelineIndex::RECT);
			}

			// Main Glyph
			auto* pGlyphImage = CVars::useMSDF ? g_msdfTextAtlas->get_glyph_info(font, glyphID, texCoordExtents,
					glyphSize, offset, glyphHasColor)
					: g_textAtlas->get_glyph_info(font, glyphID, texCoordExtents, glyphSize, offset,
					glyphHasColor);
			auto textColor = glyphHasColor ? Color{1.f, 1.f, 1.f, 1.f} : iter.get_color();

			emit_rect(lineX + pX + offset[0], paragraphLayout.textStartY + lineY + pY + offset[1], glyphSize[0],
					glyphSize[1], texCoordExtents, pGlyphImage, textColor,
					CVars::useMSDF ? PipelineIndex::MSDF : PipelineIndex::RECT);
			
			// Underline
			if ((event & Text::FormattingEvent::UNDERLINE_END) != Text::FormattingEvent::NONE) {
				auto height = font.get_underline_thickness() + 0.5f;
				emit_rect(lineX + underlineStartPos, paragraphLayout.textStartY + lineY
						+ font.get_underline_position(), pX - underlineStartPos, height, iter.get_prev_color(),
						PipelineIndex::RECT);
			}

			if ((event & Text::FormattingEvent::UNDERLINE_BEGIN) != Text::FormattingEvent::NONE) {
				underlineStartPos = pX;
			}

			// Strikethrough
			if ((event & Text::FormattingEvent::STRIKETHROUGH_END) != Text::FormattingEvent::NONE) {
				auto height = run.pFont->get_strikethrough_thickness() + 0.5f;
				emit_rect(lineX + strikethroughStartPos, paragraphLayout.textStartY + lineY
						+ font.get_strikethrough_position(), pX - strikethroughStartPos, height,
						iter.get_prev_color(), PipelineIndex::RECT);
			}

			if ((event & Text::FormattingEvent::STRIKETHROUGH_BEGIN) != Text::FormattingEvent::NONE) {
				strikethroughStartPos = pX;
			}
		}

		// Finalize last strikethrough
		if (iter.has_strikethrough()) {
			auto strikethroughEndPos = paragraphLayout.glyphPositions[glyphPosIndex];
			auto height = font.get_strikethrough_thickness() + 0.5f;
			emit_rect(lineX + strikethroughStartPos, paragraphLayout.textStartY + lineY
					+ font.get_strikethrough_position(), strikethroughEndPos - strikethroughStartPos, height,
					iter.get_color(), PipelineIndex::RECT);
		}

		// Finalize last underline
		if (iter.has_underline()) {
			auto underlineEndPos = paragraphLayout.glyphPositions[glyphPosIndex];
			auto height = font.get_underline_thickness() + 0.5f;
			emit_rect(lineX + underlineStartPos, paragraphLayout.textStartY + lineY
					+ font.get_underline_position(), underlineEndPos - underlineStartPos, height,
					iter.get_color(), PipelineIndex::RECT);
		}

		glyphPosIndex += 2;
	});

	// Debug render run outlines
	if (CVars::showRunOutlines) {
		paragraphLayout.for_each_run(m_size[0], m_textXAlignment, [&](auto lineIndex, auto runIndex, auto lineX,
				auto lineY) {
			auto* positions = paragraphLayout.get_run_positions(runIndex);
			auto minBound = positions[0];
			auto maxBound = positions[2 * paragraphLayout.get_run_glyph_count(runIndex)]; 
			emit_rect(lineX + minBound, lineY - paragraphLayout.lines[lineIndex].ascent, maxBound - minBound,
					paragraphLayout.get_line_height(lineIndex), {0, 0.5f, 0, 1}, PipelineIndex::OUTLINE);
		});
	}

	// Debug render glyph boundaries
	if (CVars::showGlyphBoundaries) {
		paragraphLayout.for_each_run(m_size[0], m_textXAlignment, [&](auto lineIndex, auto runIndex, auto lineX,
				auto lineY) {
			auto* positions = paragraphLayout.get_run_positions(runIndex);

			for (le_int32 i = 0; i <= paragraphLayout.get_run_glyph_count(runIndex); ++i) {
				emit_rect(lineX + positions[2 * i], lineY - paragraphLayout.lines[lineIndex].ascent, 0.5f,
						paragraphLayout.get_line_height(lineIndex), {0, 0.5f, 0, 1}, PipelineIndex::OUTLINE);
			}
		});
	}
}

void TextBox::emit_rect(float x, float y, float width, float height, const float* texCoords, Image* texture,
		const Color& color, PipelineIndex pipeline) {
	m_textRects.push_back({
		.x = x,
		.y = y,
		.width = width,
		.height = height,
		.texCoords = {texCoords[0], texCoords[1], texCoords[2], texCoords[3]},
		.texture = texture,
		.color = color,
		.pipeline = pipeline,
	});
}

void TextBox::emit_rect(float x, float y, float width, float height, const Color& color,
		PipelineIndex pipeline) {
	m_textRects.push_back({
		.x = x,
		.y = y,
		.width = width,
		.height = height,
		.texture = g_textAtlas->get_default_texture(),
		.color = color,
		.pipeline = pipeline,
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
		case PostLayoutCursorMoveType::MOUSE_POSITION:
		{
			auto& mouseOp = static_cast<const CursorToMouse&>(op);
			auto lineIndex = paragraphLayout.get_closest_line_to_height(static_cast<float>(mouseOp.mouseY));

			if (lineIndex == paragraphLayout.lines.size()) {
				lineIndex = paragraphLayout.lines.size() - 1;
			}

			return paragraphLayout.find_closest_cursor_position(textWidth, textXAlignment, *g_charBreakIter,
					lineIndex, static_cast<float>(mouseOp.mouseX));
		}
			break;
		default:
			break;
	}

	return cursor;
}

static bool is_line_break(UChar32 c) {
	return c == CH_LF || c == CH_CR || c == CH_LSEP || c == CH_PSEP;
}

