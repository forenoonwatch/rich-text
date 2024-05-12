#include "cursor_controller.hpp"

#include "layout_info.hpp"

#include <unicode/brkiter.h>
#include <unicode/utext.h>

using namespace Text;

static constexpr const UChar32 CH_LF = 0x000A;
static constexpr const UChar32 CH_CR = 0x000D;
static constexpr const UChar32 CH_LSEP = 0x2028;
static constexpr const UChar32 CH_PSEP = 0x2029;

static bool is_line_break(UChar32 c);

CursorController::CursorController() {
	UErrorCode errc{};
	m_iter = icu::BreakIterator::createCharacterInstance(icu::Locale::getDefault(), errc);
}

CursorController::~CursorController() {
	if (m_iter) {
		delete m_iter;
	}
}

CursorController::CursorController(CursorController&& other) noexcept {
	*this = std::move(other);
}

CursorController& CursorController::operator=(CursorController&& other) noexcept {
	std::swap(m_iter, other.m_iter);
	return *this;
}

void CursorController::set_text(std::string_view str) {
	UErrorCode errc{};
	UText uText UTEXT_INITIALIZER;
	utext_openUTF8(&uText, str.data(), str.size(), &errc);
	m_iter->setText(&uText, errc);
	m_text = std::move(str);
}

CursorPosition CursorController::next_character(CursorPosition cursor) {
	if (auto nextIndex = m_iter->following(cursor.get_position()); nextIndex != icu::BreakIterator::DONE) {
		return {static_cast<uint32_t>(nextIndex)};
	}

	return cursor;
}

CursorPosition CursorController::prev_character(CursorPosition cursor) {
	if (auto nextIndex = m_iter->preceding(cursor.get_position()); nextIndex != icu::BreakIterator::DONE) {
		return {static_cast<uint32_t>(nextIndex)};
	}

	return cursor;
}

CursorPosition CursorController::next_word(CursorPosition cursor) {
	UChar32 c;
	U8_GET((const uint8_t*)m_text.data(), 0, cursor.get_position(), m_text.size(), c);
	bool lastWhitespace = u_isWhitespace(c);

	for (;;) {
		auto nextIndex = m_iter->following(cursor.get_position());

		if (nextIndex == icu::BreakIterator::DONE) {
			break;
		}

		cursor = {static_cast<uint32_t>(nextIndex)};

		U8_GET((const uint8_t*)m_text.data(), 0, nextIndex, m_text.size(), c);
		bool whitespace = u_isWhitespace(c);

		if (!whitespace && lastWhitespace || is_line_break(c)) {
			break;
		}

		lastWhitespace = whitespace;
	}

	return cursor;
}

CursorPosition CursorController::prev_word(CursorPosition cursor) {
	UChar32 c;
	bool lastWhitespace = true;

	for (;;) {
		auto nextIndex = m_iter->preceding(cursor.get_position());

		if (nextIndex == icu::BreakIterator::DONE) {
			break;
		}

		U8_GET((const uint8_t*)m_text.data(), 0, nextIndex, m_text.size(), c);

		bool whitespace = u_isWhitespace(c);

		if (whitespace && !lastWhitespace) {
			break;
		}

		if (is_line_break(c)) {
			return {static_cast<uint32_t>(nextIndex)};
		}

		cursor = {static_cast<uint32_t>(nextIndex)};
		lastWhitespace = whitespace;
	}

	return cursor;
}

CursorPosition CursorController::closest_in_line(const LayoutInfo& layout, float textAreaWidth,
		TextXAlignment textXAlignment, size_t lineIndex, float posX) {
	return layout.find_closest_cursor_position(textAreaWidth, textXAlignment, *m_iter, lineIndex, posX);
}

CursorPosition CursorController::closest_to_position(const LayoutInfo& layout, float textAreaWidth,
		TextXAlignment textXAlignment, float posX, float posY) {
	auto lineIndex = layout.get_closest_line_to_height(posY);

	if (lineIndex == layout.get_line_count()) {
		lineIndex = layout.get_line_count() - 1;
	}

	return layout.find_closest_cursor_position(textAreaWidth, textXAlignment, *m_iter, lineIndex, posX);
}

// Static Functions

static bool is_line_break(UChar32 c) {
	return c == CH_LF || c == CH_CR || c == CH_LSEP || c == CH_PSEP;
}

