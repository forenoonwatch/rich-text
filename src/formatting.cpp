#include "formatting.hpp"

#include "font_registry.hpp"
#include "value_run_builder.hpp"
#include "utf_conversion_util.hpp"

#include <charconv>
#include <sstream>
#include <string_view>
#include <type_traits>

#include <cstring>

using namespace Text;

static constexpr const char SENTINEL = static_cast<char>(UINT8_MAX);

static constexpr bool is_space(char c) {
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

namespace {

struct FontAttributes {
	FontFamily family{}; 
	uint32_t size{};
	Color color;
	FontWeight weight{FontWeight::REGULAR};
	bool colorChange{false};
	bool sizeChange{false};
	bool weightChange{false};
};

class FormattingParser {
	public:
		explicit FormattingParser(const std::string& text, Font baseFont, Color&& baseColor,
				const StrokeState& baseStroke);

		void parse();

		FormattingRuns get_result(std::string& contentText);
		bool has_error() const;
	private:
		const char* m_iter;
		const char* m_end;
		std::ostringstream m_output;
		bool m_error{false};

		const std::string& m_text;
		
		ValueRunBuilder<Font> m_fontRuns;
		ValueRunBuilder<Color> m_colorRuns;
		ValueRunBuilder<StrokeState> m_strokeRuns;
		ValueRunBuilder<bool> m_strikethroughRuns;
		ValueRunBuilder<bool> m_underlineRuns;

		void parse_content(std::string_view expectedClose);
		bool parse_open_bracket(std::string_view expectedClose);

		void parse_comment();

		void parse_b_tag();
		void parse_s_tag();
		void parse_u_tag();

		void parse_font();
		[[nodiscard]] FontAttributes parse_font_attributes();
		void parse_font_face(FontAttributes&);

		void parse_strikethrough();
		void parse_underline();

		void parse_italic();

		void parse_stroke();
		[[nodiscard]] StrokeState parse_stroke_attributes();
		void parse_stroke_joins(StrokeState&);

		std::string_view parse_attribute(std::string_view name);
		template <typename T> requires std::is_arithmetic_v<T>
		void parse_attribute(std::string_view name, T& value);
		void parse_attribute(std::string_view name, Color& value);
		void parse_attribute(std::string_view name, FontWeight& value);

		void parse_line_break();

		bool parse_color(uint32_t&);
		bool parse_color_hex(uint32_t&);
		bool parse_color_rgb(uint32_t&);

		bool consume_char(char);
		bool consume_word(std::string_view);
		char next_char();
		int32_t get_current_string_index() const;
		void raise_error();

		void finalize_runs();
};

}

// RichText API

FormattingRuns Text::make_default_formatting_runs(const std::string& text, std::string& contentText,
		Font baseFont, Color baseColor, const StrokeState& baseStroke) {
	contentText = text;
	auto length = static_cast<int32_t>(text.size());

	return {
		.fontRuns{baseFont, length},
		.colorRuns{std::move(baseColor), length},
		.strokeRuns{baseStroke, length},
		.strikethroughRuns{false, length},
		.underlineRuns{false, length},
	};
}

FormattingRuns Text::parse_inline_formatting(const std::string& text, std::string& contentText,
		Font baseFont, Color baseColor, const StrokeState& baseStroke) {
	FormattingParser parser(text, baseFont, std::move(baseColor), baseStroke);
	parser.parse();
	return parser.get_result(contentText);
}

template <typename T>
static void convert_runs(ValueRuns<T>& runs, const std::string& srcText, const char16_t* dstText,
		int32_t dstTextLength) {
	uint32_t srcCounter{};
	uint32_t dstCounter{};

	for (size_t i = 0; i < runs.get_run_count(); ++i) {
		auto limit = runs.get_run_limit(i);
		limit = static_cast<int32_t>(utf8_index_to_utf16(srcText.data(), srcText.size(), dstText, dstTextLength,
				limit, srcCounter, dstCounter));
	}
}

void Text::convert_formatting_runs_to_utf16(FormattingRuns& runs, const std::string& contentText, 
		const char16_t* dstText, int32_t dstTextLength) {
	convert_runs(runs.fontRuns, contentText, dstText, dstTextLength);
	convert_runs(runs.colorRuns, contentText, dstText, dstTextLength);
	convert_runs(runs.strokeRuns, contentText, dstText, dstTextLength);
	convert_runs(runs.strikethroughRuns, contentText, dstText, dstTextLength);
	convert_runs(runs.underlineRuns, contentText, dstText, dstTextLength);
}

// FormattingParser

FormattingParser::FormattingParser(const std::string& text, Font baseFont, Color&& baseColor,
			const StrokeState& baseStroke)
		: m_iter(text.data())
		, m_end(text.data() + text.size())
		, m_text(text)
		, m_fontRuns{baseFont}
		, m_colorRuns{std::move(baseColor)}
		, m_strokeRuns{baseStroke}
		, m_strikethroughRuns{false}
		, m_underlineRuns{false} {}

FormattingRuns FormattingParser::get_result(std::string& contentText) {
	if (m_error) {
		return make_default_formatting_runs(m_text, contentText, m_fontRuns.get_base_value(),
				m_colorRuns.get_base_value(), m_strokeRuns.get_base_value());
	}
	else {
		contentText = m_output.str();

		FormattingRuns result{
			.fontRuns = m_fontRuns.get(),
			.colorRuns = m_colorRuns.get(),
			.strokeRuns = m_strokeRuns.get(),
			.strikethroughRuns = m_strikethroughRuns.get(),
			.underlineRuns = m_underlineRuns.get(),
		};

		return result;
	}
}

bool FormattingParser::has_error() const {
	return m_error;
}

void FormattingParser::parse() {
	parse_content("");
}

void FormattingParser::parse_content(std::string_view expectedClose) {
	for (;;) {
		auto c = next_char();

		if (c == SENTINEL) {
			if (expectedClose.empty()) {
				finalize_runs();
			}
			else {
				raise_error();
			}

			return;
		}
		else if (c == '<') {
			if (parse_open_bracket(expectedClose)) {
				return;
			}
		}
		else {
			m_output.put(c);
		}

		if (m_error) {
			return;
		}
	}
}

bool FormattingParser::parse_open_bracket(std::string_view expectedClose) {
	switch (next_char()) {
		case '!':
			parse_comment();
			break;
		case '/':
			if (expectedClose.empty()) {
				raise_error();
			}
			else {
				consume_word(std::move(expectedClose));
			}
			return true;
		case 'f':
			parse_font();
			break;
		case 's':
			parse_s_tag();
			break;
		case 'u':
			parse_u_tag();
			break;
		default:
			raise_error();
			return true;
	}

	return false;
}

void FormattingParser::parse_comment() {
	if (!consume_char('-')) {
		return;
	}

	if (!consume_char('-')) {
		return;
	}

	for (;;) {
		auto c = next_char();

		if (c == SENTINEL) {
			raise_error();
			return;
		}
		else if (c == '-') {
			if (!consume_char('-')) {
				return;
			}

			if (!consume_char('>')) {
				return;
			}

			break;
		}
	}
}

void FormattingParser::parse_s_tag() {
	switch (next_char()) {
		case '>':
			parse_strikethrough();
			break;
		case 'c':
			if (!consume_char('>')) {
				return;
			}

			// FIXME: parse_smallcaps();
			break;
		case 't':
			parse_stroke();
			break;
		default:
			raise_error();
			break;
	}
}

void FormattingParser::parse_u_tag() {
	switch (next_char()) {
		case '>':
			parse_underline();
			break;
		case 'c':
			if (!consume_char('>')) {
				raise_error();
				return;
			}

			// FIXME: parse_uppercase();
			break;
		case 'p':
			if (!consume_word("percase>")) {
				raise_error();
				return;
			}

			// FIXME: parse_uppercase();
			break;
		default:
			raise_error();
			break;
	}
}

void FormattingParser::parse_font() {
	if (!consume_word("ont")) {
		return;
	}

	auto fontAttribs = parse_font_attributes();

	auto currFont = m_fontRuns.get_current_value();
	bool hasFontChange = (fontAttribs.family && fontAttribs.family != currFont.get_family())
			|| (fontAttribs.sizeChange && fontAttribs.size != currFont.get_size())
			|| (fontAttribs.weightChange &&  fontAttribs.weight != currFont.get_weight());

	if (hasFontChange) {
		auto family = fontAttribs.family ? fontAttribs.family : currFont.get_family();
		auto size = fontAttribs.sizeChange ? fontAttribs.size : currFont.get_size();
		auto weight = fontAttribs.weightChange ? fontAttribs.weight : currFont.get_weight();
		Font newFont(family, weight, currFont.get_style(), size);

		m_fontRuns.push(m_output.view().size(), newFont);
	}

	if (fontAttribs.colorChange) {
		m_colorRuns.push(m_output.view().size(), fontAttribs.color); 
	}

	parse_content("font>");

	if (hasFontChange) {
		m_fontRuns.pop(m_output.view().size());
	}

	if (fontAttribs.colorChange) {
		m_colorRuns.pop(m_output.view().size());
	}
}

FontAttributes FormattingParser::parse_font_attributes() {
	FontAttributes result{};

	for (;;) {
		switch (next_char()) {
			case 'c':
				parse_attribute("olor=\"", result.color);
				result.colorChange = true;
				break;
			case 'f':
				parse_font_face(result);
				break;
			case 's':
				parse_attribute("ize=\"", result.size);
				result.sizeChange = true;
				break;
			case 'w':
				parse_attribute("eight=\"", result.weight);
				result.weightChange = true;
				break;
			case ' ':
				break;
			case '>':
				return result;
			default:
				raise_error();
				return result;
		}
	}

	return result;
}

void FormattingParser::parse_font_face(FontAttributes& attribs) {
	auto faceName = parse_attribute("ace=\"");
	if (faceName.empty()) {
		raise_error();
		return;
	}

	attribs.family = FontRegistry::get_family(faceName);

	if (!attribs.family) {
		raise_error();
	}
}

void FormattingParser::parse_strikethrough() {
	m_strikethroughRuns.push(m_output.view().size(), true);
	parse_content("s>");
	m_strikethroughRuns.pop(m_output.view().size());
}

void FormattingParser::parse_underline() {
	m_underlineRuns.push(m_output.view().size(), true);
	parse_content("u>");
	m_underlineRuns.pop(m_output.view().size());
}

void FormattingParser::parse_stroke() {
	if (!consume_word("roke")) {
		raise_error();
		return;
	}

	auto state = parse_stroke_attributes();

	m_strokeRuns.push(m_output.view().size(), state); 

	parse_content("stroke>");

	m_strokeRuns.pop(m_output.view().size());
}

StrokeState FormattingParser::parse_stroke_attributes() {
	StrokeState result{
		.color = {0.f, 0.f, 0.f, 1.f}, 
		.thickness = 1,
		.joins = StrokeType::ROUND,
	};

	for (;;) {
		switch (next_char()) {
			case 'c':
			{
				Color c{};
				parse_attribute("olor=\"", c);
				result.color = {c.r, c.g, c.b, result.color.a};
			}
				break;
			case 'j':
				parse_stroke_joins(result);
				break;
			case 't':
				switch (next_char()) {
					case 'h':
						parse_attribute("ickness=\"", result.thickness);
						break;
					case 'r':
						parse_attribute("ansparency=\"", result.color.a);
						result.color.a = 1.f - result.color.a;
						break;
					default:
						raise_error();
				}

				break;
			case ' ':
				break;
			case '>':
				return result;
			default:
				raise_error();
				return result;
		}
	}

	return result;
}

void FormattingParser::parse_stroke_joins(StrokeState& attribs) {
	if (!consume_word("oins=\"")) {
		return;
	}

	auto start = get_current_string_index();

	for (;;) {
		auto end = get_current_string_index();
		auto c = next_char();

		if (c == '"') {
			std::string_view typeName(m_text.data() + start, end - start);

			if (typeName.compare("round") == 0) {
				attribs.joins = StrokeType::ROUND;
			}
			else if (typeName.compare("bevel") == 0) {
				attribs.joins = StrokeType::BEVEL;
			}
			else if (typeName.compare("miter") == 0) {
				attribs.joins = StrokeType::MITER;
			}
			else {
				raise_error();
			}
			
			return;
		}
		else if (c == SENTINEL) {
			raise_error();
			return;
		}
	}
}

std::string_view FormattingParser::parse_attribute(std::string_view name) {
	if (!consume_word(name)) {
		return {};
	}

	auto start = get_current_string_index();

	for (;;) {
		auto end = get_current_string_index();
		auto c = next_char();

		if (c == '"') {
			return std::string_view(m_text.data() + start, end - start);
		}
		else if (c == SENTINEL) {
			raise_error();
			return {};
		}
	}
}

template <typename T> requires std::is_arithmetic_v<T>
void FormattingParser::parse_attribute(std::string_view name, T& value) {
	auto attrib = parse_attribute(name);
	if (auto [ptr, ec] = std::from_chars(attrib.data(), attrib.data() + attrib.size(), value);
			ec != std::errc{}) {
		raise_error();
	}
}

void FormattingParser::parse_attribute(std::string_view name, Color& value) {
	if (!consume_word(name)) {
		return;
	}

	uint32_t color{};
	if (!parse_color(color)) {
		return;
	}

	if (!consume_char('"')) {
		return;
	}

	value = Color::from_abgr_uint(color);
	value.a = 1.f;
}

void FormattingParser::parse_attribute(std::string_view name, FontWeight& value) {
	auto attrib = parse_attribute(name);
	uint32_t numericValue{};
	char lowercaseAttrib[16]{};

	if (attrib.size() >= 16) {
		raise_error();
	}

	// Naive ASCII-only lower, insufficient for actual language applications, but since all formatting
	// controls are basic ASCII, this suffices
	for (size_t i = 0; i < attrib.size(); ++i) {
		if (auto c = attrib[i]; c >= 'A' && c <= 'Z') {
			lowercaseAttrib[i] = 'a' + (c - 'A');
		}
		else {
			lowercaseAttrib[i] = c;
		}
	}

	// Numeric weight, multiples of 100 in [100, 900] valid
	if (auto [ptr, ec] = std::from_chars(attrib.data(), attrib.data() + attrib.size(), numericValue);
			ec == std::errc{}) {
		if (numericValue >= 100 && numericValue <= 900 && numericValue % 100 == 0) {
			value = static_cast<FontWeight>(numericValue / 100 - 1);
		}
		else {
			raise_error();
		}
	}
	// Named weights
	else if (std::strcmp(lowercaseAttrib, "thin") == 0) {
		value = FontWeight::THIN;
	}
	else if (std::strcmp(lowercaseAttrib, "extra light") == 0) {
		value = FontWeight::EXTRA_LIGHT;
	}
	else if (std::strcmp(lowercaseAttrib, "light") == 0) {
		value = FontWeight::LIGHT;
	}
	else if (std::strcmp(lowercaseAttrib, "regular") == 0) {
		value = FontWeight::REGULAR;
	}
	else if (std::strcmp(lowercaseAttrib, "medium") == 0) {
		value = FontWeight::MEDIUM;
	}
	else if (std::strcmp(lowercaseAttrib, "semi bold") == 0) {
		value = FontWeight::SEMI_BOLD;
	}
	else if (std::strcmp(lowercaseAttrib, "bold") == 0) {
		value = FontWeight::BOLD;
	}
	else if (std::strcmp(lowercaseAttrib, "extra bold") == 0) {
		value = FontWeight::EXTRA_BOLD;
	}
	else if (std::strcmp(lowercaseAttrib, "black") == 0) {
		value = FontWeight::BLACK;
	}
	else {
		raise_error();
	}
}

bool FormattingParser::parse_color(uint32_t& color) {
	switch (next_char()) {
		case '#':
			return parse_color_hex(color);
		case 'r':
			return parse_color_rgb(color);
		default:
			raise_error();
			return false;
	}
}

bool FormattingParser::parse_color_hex(uint32_t& color) {
	char buffer[6]{};

	for (size_t i = 0; i < 6; ++i) {
		auto c = next_char();

		if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f')) {
			buffer[i] = c;
		}
		else {
			raise_error();
			return false;
		}
	}

	if (auto res = std::from_chars(buffer, buffer + 6, color, 16); res.ptr != buffer + 6) {
		raise_error();
		return false;
	}

	return true;
}

bool FormattingParser::parse_color_rgb(uint32_t& color) {
	static constexpr const char* stops = ",,)";

	if (!consume_word("gb(")) {
		return false;
	}

	uint8_t colorChannels[3]{};

	for (size_t channelIndex = 0; channelIndex < 3; ++channelIndex) {
		char numberBuffer[3]{};
		size_t i = 0;
		auto c = next_char();

		// Skip leading whitespace
		while (is_space(c)) {
			c = next_char();
		}

		for (;;) {
			if (c >= '0' && c <= '9' && i < 3) {
				numberBuffer[i] = c;
			}
			else if (is_space(c) || c == stops[channelIndex]) {
				std::from_chars(numberBuffer, numberBuffer + i + 1, colorChannels[channelIndex]);

				// Skip trailing whitespace
				while (c != stops[channelIndex]) {
					c = next_char();

					if (c != stops[channelIndex] && !is_space(c)) {
						raise_error();
						return false;
					}
				}

				break;
			}
			else {
				raise_error();
				return false;
			}

			c = next_char();
			++i;
		}
	}

	color = (static_cast<uint32_t>(colorChannels[0]) << 16)
			| (static_cast<uint32_t>(colorChannels[1]) << 8)
			| static_cast<uint32_t>(colorChannels[2]);

	return true;
}

bool FormattingParser::consume_char(char c) {
	if (next_char() == c) {
		return true;
	}

	raise_error();
	return false;
}

bool FormattingParser::consume_word(std::string_view word) {
	for (size_t i = 0; i < word.size(); ++i) {
		if (!consume_char(word[i])) {
			return false;
		}
	}

	return true;
}

char FormattingParser::next_char() {
	return m_iter >= m_end ? SENTINEL : *(m_iter++);
}

int32_t FormattingParser::get_current_string_index() const {
	return static_cast<int32_t>(m_iter - m_text.data());
}

void FormattingParser::raise_error() {
	m_error = true;
}

void FormattingParser::finalize_runs() {
	m_fontRuns.pop(m_output.view().size());
	m_colorRuns.pop(m_output.view().size());
	m_strokeRuns.pop(m_output.view().size());
	m_strikethroughRuns.pop(m_output.view().size());
	m_underlineRuns.pop(m_output.view().size());
}

