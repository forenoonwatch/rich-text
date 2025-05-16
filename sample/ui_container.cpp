#include "ui_container.hpp"

#include "config_vars.hpp"
#include "msdf_text_atlas.hpp"
#include "text_atlas.hpp"
#include "visitor_base.hpp"

#include "text_draw_util.hpp"

#include <GLFW/glfw3.h>

std::shared_ptr<UIContainer> UIContainer::create() {
	return std::make_shared<UIContainer>();
}

void UIContainer::emit_rect(float x, float y, float width, float height, const float* texCoords, Image* texture,
		const Text::Color& color, PipelineIndex pipeline, const Text::Pair<float, float>* pClip) {
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

void UIContainer::emit_rect(float x, float y, float width, float height, const Text::Color& color,
		PipelineIndex pipeline, const Text::Pair<float, float>* pClip) {
	float texCoords[4] = {0.f, 0.f, 1.f, 1.f};
	emit_rect(x, y, width, height, texCoords, g_textAtlas->get_default_texture(), color, pipeline, pClip);
}

void UIContainer::draw_text(const Text::LayoutInfo& layout, float positionX, float positionY,
		float textAreaWidth, Text::XAlignment textXAlignment, const Text::Color& color) {
	Text::draw_text(layout, textAreaWidth, 0.f, textXAlignment, Text::YAlignment::TOP, false, VisitorBase {
		[&](const Text::SingleScriptFont& font, uint32_t glyphIndex, float x, float y) {
			float offset[2]{};
			float texCoordExtents[4]{};
			float glyphSize[2]{};
			bool glyphHasColor{};

			auto* pGlyphImage = CVars::useMSDF ? g_msdfTextAtlas->get_glyph_info(font, glyphIndex,
							texCoordExtents, glyphSize, offset, glyphHasColor)
					: g_textAtlas->get_glyph_info(font, glyphIndex, texCoordExtents, glyphSize, offset,
							glyphHasColor);

			emit_rect(positionX + x + offset[0], positionY + y + offset[1], glyphSize[0], glyphSize[1],
					texCoordExtents, pGlyphImage, glyphHasColor ? Text::Color{1.f, 1.f, 1.f, 1.f} : color,
					CVars::useMSDF ? PipelineIndex::MSDF : PipelineIndex::RECT);

			if (CVars::showGlyphOutlines) {
				emit_rect(positionX + x + offset[0], positionY + y + offset[1], glyphSize[0], glyphSize[1],
						{0, 0.5f, 0, 1}, PipelineIndex::OUTLINE);
			}
		}
	});
}

void UIContainer::draw_text(const Text::LayoutInfo& layout, const Text::FormattingRuns& formatting,
		float positionX, float positionY, float textAreaWidth, float textAreaHeight,
		Text::XAlignment textXAlignment, Text::YAlignment textYAlignment, bool vertical,
		Text::CursorPosition inSelectionStart, Text::CursorPosition inCursorPosition) {
	bool hasHighlighting = inSelectionStart.is_valid();
	uint32_t selectionStart{};
	uint32_t selectionEnd{};

	// Add highlight ranges in a separate pass to keep from accidental clipping across runs
	if (hasHighlighting) {
		selectionStart = inSelectionStart.get_position();
		selectionEnd = inCursorPosition.get_position();

		if (selectionStart > selectionEnd) {
			std::swap(selectionStart, selectionEnd);
		}

		layout.for_each_run(textAreaWidth, 0.f, textXAlignment, Text::YAlignment::TOP, false,
				[&](auto lineIndex, auto runIndex, auto lineX, auto lineY) {
			if (layout.run_contains_char_range(runIndex, selectionStart, selectionEnd)) {
				auto [minPos, maxPos] = layout.get_position_range_in_run(runIndex, selectionStart,
						selectionEnd);
				
				emit_rect(positionX + lineX + minPos, positionY + lineY
						- layout.get_line_ascent(lineIndex), maxPos - minPos, layout.get_line_height(lineIndex),
						Text::Color::from_rgb(0, 120, 215), PipelineIndex::RECT);
			}
		});
	}

	// Draw main text rects
	bool runHasHighlighting = false;
	Text::Pair<float, float> highlightRange{};
	const Text::Pair<float, float>* pClip = nullptr;

	Text::draw_text(layout, formatting, textAreaWidth, textAreaHeight, textXAlignment, textYAlignment,
			vertical, VisitorBase {
		// Helper callback that gets called once per run. Used to set up highlighting metadata.
		[&](size_t /*lineIndex*/, size_t runIndex) {
			runHasHighlighting = hasHighlighting && layout.run_contains_char_range(runIndex, selectionStart,
					selectionEnd);

			if (runHasHighlighting) {
				highlightRange = layout.get_position_range_in_run(runIndex, selectionStart, selectionEnd);
				highlightRange.first += positionX;
				highlightRange.second += positionX;
				pClip = &highlightRange;
			}
			else {
				highlightRange = {};
				pClip = nullptr;
			}
		},
		// Callback for all non-stroke glyphs
		[&](const Text::SingleScriptFont& font, uint32_t glyphIndex, float x, float y,
				const Text::Color& color) {
			float offset[2]{};
			float texCoordExtents[4]{};
			float glyphSize[2]{};
			bool glyphHasColor{};

			auto* pGlyphImage = CVars::useMSDF ? g_msdfTextAtlas->get_glyph_info(font, glyphIndex,
							texCoordExtents, glyphSize, offset, glyphHasColor)
					: g_textAtlas->get_glyph_info(font, glyphIndex, texCoordExtents, glyphSize, offset,
							glyphHasColor);

			emit_rect(positionX + x + offset[0], positionY + y + offset[1], glyphSize[0], glyphSize[1],
					texCoordExtents, pGlyphImage, glyphHasColor ? Text::Color{1.f, 1.f, 1.f, 1.f} : color,
					CVars::useMSDF ? PipelineIndex::MSDF : PipelineIndex::RECT, pClip);

			if (CVars::showGlyphOutlines) {
				emit_rect(positionX + x + offset[0], positionY + y + offset[1], glyphSize[0], glyphSize[1],
						{0, 0.5f, 0, 1}, PipelineIndex::OUTLINE);
			}
		},
		// Callback for all stroke glyphs
		[&](const Text::SingleScriptFont& font, uint32_t glyphIndex, float x, float y,
				const Text::StrokeState& stroke) {
			float offset[2]{};
			float texCoordExtents[4]{};
			float glyphSize[2]{};
			bool strokeHasColor{};

			auto* pGlyphImage = CVars::useMSDF ? g_msdfTextAtlas->get_stroke_info(font, glyphIndex,
							stroke.thickness, stroke.joins, texCoordExtents, glyphSize, offset, strokeHasColor)
					: g_textAtlas->get_stroke_info(font, glyphIndex, stroke.thickness, stroke.joins,
							texCoordExtents, glyphSize, offset, strokeHasColor);

			emit_rect(positionX + x + offset[0], positionY + y + offset[1], glyphSize[0], glyphSize[1],
					texCoordExtents, pGlyphImage, stroke.color,
					CVars::useMSDF ? PipelineIndex::MSDF : PipelineIndex::RECT);
		},
		// Callback for all untextured rects (underline, strikethrough)
		[&](float x, float y, float width, float height, const Text::Color& color) {
			emit_rect(positionX + x, positionY + y, width, height, color, PipelineIndex::RECT, pClip);
		}
	});

	// Debug render run outlines
	if (CVars::showRunOutlines) {
		layout.for_each_run(textAreaWidth, textAreaHeight, textXAlignment, textYAlignment, vertical,
				[&](auto lineIndex, auto runIndex, auto lineX, auto lineY) {
			auto* positions = layout.get_run_positions(runIndex);
			auto minBound = positions[0];
			auto maxBound = positions[2 * layout.get_run_glyph_count(runIndex)]; 
			emit_rect(positionX + lineX + minBound,
					positionY + lineY - layout.get_line_ascent(lineIndex), maxBound - minBound,
					layout.get_line_height(lineIndex), {0, 0.5f, 0, 1}, PipelineIndex::OUTLINE);
		});
	}

	// Debug render glyph boundaries
	if (CVars::showGlyphBoundaries) {
		layout.for_each_run(textAreaWidth, textAreaHeight, textXAlignment, textYAlignment, vertical,
				[&](auto lineIndex, auto runIndex, auto lineX, auto lineY) {
			auto* positions = layout.get_run_positions(runIndex);

			for (int32_t i = 0; i <= layout.get_run_glyph_count(runIndex); ++i) {
				emit_rect(positionX + lineX + positions[2 * i],
						positionY + lineY - layout.get_line_ascent(lineIndex), 0.5f,
						layout.get_line_height(lineIndex), {0, 0.5f, 0, 1}, PipelineIndex::OUTLINE);
			}
		});
	}
}

void UIContainer::draw_text_immediate(Text::Font font, const Text::Color& color, std::string_view text, float x,
		float y, float width, float height, Text::XAlignment textXAlignment, Text::YAlignment textYAlignment) {
	Text::LayoutInfo layout{};
	Text::ValueRuns<Text::Font> fontRuns(font, text.size());

	Text::LayoutBuildParams params{
		.textAreaWidth = width,
		.textAreaHeight = height,
		.tabWidth = 8.f,
		.xAlignment = textXAlignment,
		.yAlignment = textYAlignment,
	};
	m_layoutBuilder.build_layout_info(layout, text.data(), text.size(), fontRuns, params);

	draw_text(layout, x, y, width, textXAlignment, color);
}

void UIContainer::render() {
	m_pipelineIndex = PipelineIndex::INVALID;
	m_pipeline = nullptr;
	m_texture = nullptr;

	for_each_child([this](auto& child) {
		child.render_internal(*this);
		return IterationDecision::CONTINUE;
	});
}

bool UIContainer::handle_mouse_button(int button, int action, int mods, double mouseX, double mouseY) {
	bool focusedOnObject = false;

	m_mouseButtonsDown[button] = action != GLFW_RELEASE;

	auto iterDec = for_each_descendant_bottom_up([&](auto& desc) {
		if (!desc.is_visible_from_ancestors()) {
			return IterationDecision::CONTINUE;
		}

		auto iterDec = desc.handle_mouse_button(*this, button, action, mods, mouseX, mouseY)
				? IterationDecision::BREAK : IterationDecision::CONTINUE;

		// If the object passes through mouse input, consider it unfocused
		if (action == GLFW_PRESS && button == GLFW_MOUSE_BUTTON_1 && iterDec == IterationDecision::BREAK) {
			focusedOnObject = true;
			focus_object(desc);
		}

		return iterDec;
	});

	if (action == GLFW_PRESS && button == GLFW_MOUSE_BUTTON_1 && !focusedOnObject) {
		release_focused_object();
	}

	return iterDec == IterationDecision::BREAK;
}

bool UIContainer::handle_key_press(int key, int action, int mods, double mouseX, double mouseY) {
	auto iterDec = for_each_descendant_bottom_up([&](auto& desc) {
		if (desc.is_mouse_inside(mouseX, mouseY)) {
			return desc.handle_key_press(*this, key, action, mods) ? IterationDecision::BREAK
					: IterationDecision::CONTINUE;
		}
		
		return IterationDecision::CONTINUE;
	});

	return iterDec == IterationDecision::BREAK;
}

bool UIContainer::handle_mouse_move(double mouseX, double mouseY) {
	m_mouseX = mouseX;
	m_mouseY = mouseY;

	UIObject* pHoveredObject = nullptr;

	auto iterDec = for_each_descendant([&](auto& desc) {
		if (desc.is_mouse_inside(mouseX, mouseY)) {
			pHoveredObject = &desc;
		}

		return desc.handle_mouse_move(*this, mouseX, mouseY) ? IterationDecision::BREAK
				: IterationDecision::CONTINUE;
	});

	auto pLastHoveredObject = m_hoveredObject.lock();

	if (pLastHoveredObject.get() && pLastHoveredObject.get() != pHoveredObject) {
		pLastHoveredObject->handle_mouse_leave(*this);
	}

	if (pHoveredObject) {
		pHoveredObject->handle_mouse_enter(*this, mouseX, mouseY);
		m_hoveredObject = pHoveredObject->weak_from_this();
	}

	return iterDec == IterationDecision::BREAK;
}

bool UIContainer::handle_text_input(unsigned codepoint) {
	auto iterDec = for_each_descendant([&](auto& desc) {
		return desc.handle_text_input(*this, codepoint)
			? IterationDecision::BREAK : IterationDecision::CONTINUE;
	});

	return iterDec == IterationDecision::BREAK;
}

void UIContainer::handle_focus_lost() {
	release_focused_object();
}

void UIContainer::focus_object(UIObject& object) {
	if (m_focusedObject.lock().get() == &object) {
		return;
	}

	release_focused_object();

	m_focusedObject = object.weak_from_this();
	object.m_focused = true;
	object.handle_focused(*this);
}

void UIContainer::release_focused_object() {
	if (auto pObject = m_focusedObject.lock()) {
		pObject->m_focused = false;
		pObject->handle_focus_lost(*this);
	}

	m_focusedObject = {};
	m_clickCount = 0;
	m_lastClickPos = {Text::CursorPosition::INVALID_VALUE};
}

double UIContainer::get_mouse_x() const {
	return m_mouseX;
}

double UIContainer::get_mouse_y() const {
	return m_mouseY;
}

bool UIContainer::is_mouse_button_down(int mouseButton) const {
	return m_mouseButtonsDown[mouseButton];
}

uint32_t UIContainer::text_box_click(Text::CursorPosition pos) {
	auto time = glfwGetTime();

	if (pos == m_lastClickPos && time - m_lastClickTime <= DOUBLE_CLICK_TIME) {
		++m_clickCount;
	}
	else {
		m_clickCount = 0;
	}

	m_lastClickTime = time;
	m_lastClickPos = pos;

	return m_clickCount;
}

void UIContainer::draw_rect_internal(float x, float y, float width, float height, const float* texCoords,
		Image* texture, const Text::Color& color, PipelineIndex pipeline) {
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

