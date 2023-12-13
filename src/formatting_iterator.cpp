#include "formatting_iterator.hpp"
#include "formatting.hpp"

using namespace Text;

template <typename T>
static uint32_t advance_run(const ValueRuns<T>& runs, uint32_t runIndex, uint32_t charIndex);

FormattingIterator::FormattingIterator(const FormattingRuns& fmt, uint32_t charIndex)
		: m_formatting(&fmt)
		, m_colorRunIndex(fmt.colorRuns.get_run_containing_index(charIndex))
		, m_strokeRunIndex(fmt.strokeRuns.get_run_containing_index(charIndex))
		, m_strikethroughRunIndex(fmt.strikethroughRuns.get_run_containing_index(charIndex))
		, m_underlineRunIndex(fmt.underlineRuns.get_run_containing_index(charIndex))
		, m_color{fmt.colorRuns.get_run_value(m_colorRunIndex)}
		, m_strikethrough{false}
		, m_underline{false} {}

FormattingEvent FormattingIterator::advance_to(uint32_t charIndex) {
	m_strokeRunIndex = advance_run(m_formatting->strokeRuns, m_strokeRunIndex, charIndex);
	m_colorRunIndex = advance_run(m_formatting->colorRuns, m_colorRunIndex, charIndex);
	m_strikethroughRunIndex = advance_run(m_formatting->strikethroughRuns, m_strikethroughRunIndex, charIndex);
	m_underlineRunIndex = advance_run(m_formatting->underlineRuns, m_underlineRunIndex, charIndex);

	auto color = m_formatting->colorRuns.get_run_value(m_colorRunIndex);
	bool strikethrough = m_formatting->strikethroughRuns.get_run_value(m_strikethroughRunIndex);
	bool underline = m_formatting->underlineRuns.get_run_value(m_underlineRunIndex);
	bool colorChanged = color != m_color;

	auto event = static_cast<FormattingEvent>(
			static_cast<uint32_t>(FormattingEvent::STRIKETHROUGH_BEGIN)
					* (strikethrough && (!m_strikethrough || colorChanged))
			| static_cast<uint32_t>(FormattingEvent::STRIKETHROUGH_END)
					* ((!strikethrough && m_strikethrough) || (strikethrough && colorChanged))
			| static_cast<uint32_t>(FormattingEvent::UNDERLINE_BEGIN)
					* (underline && (!m_underline || colorChanged))
			| static_cast<uint32_t>(FormattingEvent::UNDERLINE_END)
					* ((!underline && m_underline) || (underline && colorChanged)));

	m_prevColor = m_color;
	m_color = color;
	m_strikethrough = strikethrough;
	m_underline = underline;

	return event;
}

const Color& FormattingIterator::get_color() const {
	return m_color;
}

const Color& FormattingIterator::get_prev_color() const {
	return m_prevColor;
}

StrokeState FormattingIterator::get_stroke_state() const {
	return m_formatting->strokeRuns.get_run_value(m_strokeRunIndex);
}

bool FormattingIterator::has_strikethrough() const {
	return m_strikethrough;
}

bool FormattingIterator::has_underline() const {
	return m_underline;
}

template <typename T>
static uint32_t advance_run(const ValueRuns<T>& runs, uint32_t runIndex, uint32_t charIndex) {
	while (runIndex +  1 < runs.get_run_count() && charIndex >= runs.get_run_limit(runIndex)) {
		++runIndex;
	}

	while (runIndex > 0 && charIndex < runs.get_run_limit(runIndex - 1)) {
		--runIndex;
	}

	return runIndex;
}

