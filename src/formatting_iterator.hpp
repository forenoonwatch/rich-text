#pragma once

#include "common.hpp"
#include "formatting.hpp"

namespace Text {

struct FormattingRuns;

enum class FormattingEvent : uint32_t {
	NONE = 0,
	STRIKETHROUGH_BEGIN = 1,
	STRIKETHROUGH_END = 2,
	UNDERLINE_BEGIN = 4,
	UNDERLINE_END = 8,
};

RICHTEXT_DEFINE_ENUM_BITFLAG_OPERATORS(FormattingEvent)

class FormattingIterator {
	public:
		explicit FormattingIterator(const FormattingRuns&, uint32_t initialCharIndex);

		FormattingEvent advance_to(uint32_t charIndex);

		const Color& get_color() const;
		const Color& get_prev_color() const;
		StrokeState get_stroke_state() const;
		bool has_strikethrough() const;
		bool has_underline() const;
	private:
		const FormattingRuns* m_formatting;
		uint32_t m_colorRunIndex;
		uint32_t m_strokeRunIndex;
		uint32_t m_strikethroughRunIndex;
		uint32_t m_underlineRunIndex;
		Color m_color;
		Color m_prevColor;
		bool m_strikethrough;
		bool m_underline;
};

}

