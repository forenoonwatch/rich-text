#include "rich_text.hpp"

#include "font.hpp"
#include "font_cache.hpp"
#include "multi_script_font.hpp"
#include "text_run_builder.hpp"

#include <unicode/utext.h>

#include <charconv>
#include <string_view>
#include <type_traits>

using namespace RichText;

constexpr bool is_space(UChar32 c) {
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

namespace {

struct FontAttributes {
	FamilyIndex_T family{FontCache::INVALID_FAMILY}; 
	uint32_t size{};
	Color color;
	bool colorChange{false};
	bool sizeChange{false};
};

class RichTextParser {
	public:
		explicit RichTextParser(const std::string& text, const MultiScriptFont& baseFont, Color&& baseColor,
				const StrokeState& baseStroke);

		void parse();

		RichText::Result get_result(std::string& contentText);
		bool has_error() const;
	private:
		UText m_iter UTEXT_INITIALIZER;
		icu::UnicodeString m_output;
		bool m_error{false};

		const std::string& m_text;
		
		TextRunBuilder<uint32_t> m_fontRuns;
		TextRunBuilder<Color> m_colorRuns;
		TextRunBuilder<StrokeState> m_strokeRuns;
		TextRunBuilder<bool> m_strikethroughRuns;
		TextRunBuilder<bool> m_underlineRuns;
		std::vector<MultiScriptFont> m_ownedFonts;

		void parse_content(std::u32string_view expectedClose);
		bool parse_open_bracket(std::u32string_view expectedClose);

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

		std::string_view parse_attribute(std::u32string_view name);
		template <typename T> requires std::is_arithmetic_v<T>
		void parse_attribute(std::u32string_view name, T& value);
		void parse_attribute(std::u32string_view name, Color& value);

		void parse_line_break();

		bool parse_color(uint32_t&);
		bool parse_color_hex(uint32_t&);
		bool parse_color_rgb(uint32_t&);

		bool consume_char(UChar32);
		bool consume_word(std::u32string_view);
		void raise_error();

		void finalize_runs();
};

}

// RichText API

RichText::Result RichText::make_default_runs(const std::string& text, std::string& contentText,
		const MultiScriptFont& baseFont, Color baseColor, const StrokeState& baseStroke) {
	contentText = text;
	auto str = icu::UnicodeString::fromUTF8(text);
	auto length = str.length();
	std::vector<MultiScriptFont> ownedFonts{baseFont};

	return {
		.str = std::move(str),
		.fontRuns{&ownedFonts.front(), length},
		.colorRuns{std::move(baseColor), length},
		.strokeRuns{baseStroke, length},
		.strikethroughRuns{false, length},
		.underlineRuns{false, length},
		.ownedFonts = std::move(ownedFonts),
	};
}

RichText::Result RichText::parse(const std::string& text, std::string& contentText,
		const MultiScriptFont& baseFont, Color baseColor, const StrokeState& baseStroke) {
	RichTextParser parser(text, baseFont, std::move(baseColor), baseStroke);
	parser.parse();
	return parser.get_result(contentText);
}

// RichTextParser

RichTextParser::RichTextParser(const std::string& text, const MultiScriptFont& baseFont, Color&& baseColor,
			const StrokeState& baseStroke)
		: m_text(text)
		, m_fontRuns{0u}
		, m_colorRuns{std::move(baseColor)}
		, m_strokeRuns{baseStroke}
		, m_strikethroughRuns{false}
		, m_underlineRuns{false}
		, m_ownedFonts{baseFont} {
	UErrorCode errc{};
	utext_openUTF8(&m_iter, text.data(), text.size(), &errc);
}

RichText::Result RichTextParser::get_result(std::string& contentText) {
	if (m_error) {
		return RichText::make_default_runs(m_text, contentText, m_ownedFonts.front(),
				m_colorRuns.get_base_value(), m_strokeRuns.get_base_value());
	}
	else {
		m_output.toUTF8String(contentText);
		auto fontIndexRuns = m_fontRuns.get();

		RichText::Result result{
			.str = std::move(m_output),
			.fontRuns{fontIndexRuns.get_value_count()},
			.colorRuns = m_colorRuns.get(),
			.strokeRuns = m_strokeRuns.get(),
			.strikethroughRuns = m_strikethroughRuns.get(),
			.underlineRuns = m_underlineRuns.get(),
			.ownedFonts = std::move(m_ownedFonts),
		};

		for (uint32_t i = 0; i < fontIndexRuns.get_value_count(); ++i) {
			result.fontRuns.add(fontIndexRuns.get_limits()[i],
					&result.ownedFonts[fontIndexRuns.get_values()[i]]);
		}

		return result;
	}
}

bool RichTextParser::has_error() const {
	return m_error;
}

void RichTextParser::parse() {
	parse_content(U"");
}

void RichTextParser::parse_content(std::u32string_view expectedClose) {
	for (;;) {
		auto c = UTEXT_NEXT32(&m_iter);

		if (c == U_SENTINEL) {
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
			m_output.append(c);
		}

		if (m_error) {
			return;
		}
	}
}

bool RichTextParser::parse_open_bracket(std::u32string_view expectedClose) {
	switch (UTEXT_NEXT32(&m_iter)) {
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

void RichTextParser::parse_comment() {
	if (!consume_char('-')) {
		return;
	}

	if (!consume_char('-')) {
		return;
	}

	for (;;) {
		auto c = UTEXT_NEXT32(&m_iter);

		if (c == U_SENTINEL) {
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

void RichTextParser::parse_s_tag() {
	switch (UTEXT_NEXT32(&m_iter)) {
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

void RichTextParser::parse_u_tag() {
	switch (UTEXT_NEXT32(&m_iter)) {
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
			if (!consume_word(U"percase>")) {
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

void RichTextParser::parse_font() {
	if (!consume_word(U"ont")) {
		return;
	}

	auto fontAttribs = parse_font_attributes();

	auto& currFont = m_ownedFonts[m_fontRuns.get_current_value()];
	bool hasFontChange = (fontAttribs.family != FontCache::INVALID_FAMILY
			&& fontAttribs.family != currFont.get_family())
			|| (fontAttribs.sizeChange && fontAttribs.size != currFont.get_size());

	if (hasFontChange) {
		auto family = fontAttribs.family != FontCache::INVALID_FAMILY ? fontAttribs.family
				: currFont.get_family();
		auto size = fontAttribs.sizeChange ? fontAttribs.size : currFont.get_size();
		auto newFontIndex = static_cast<uint32_t>(m_ownedFonts.size());
		m_ownedFonts.emplace_back(currFont.get_font_cache()->get_font(family, currFont.get_weight(),
				currFont.get_style(), size));

		m_fontRuns.push(m_output.length(), newFontIndex);
	}

	if (fontAttribs.colorChange) {
		m_colorRuns.push(m_output.length(), fontAttribs.color); 
	}

	parse_content(U"font>");

	if (hasFontChange) {
		m_fontRuns.pop(m_output.length());
	}

	if (fontAttribs.colorChange) {
		m_colorRuns.pop(m_output.length());
	}
}

FontAttributes RichTextParser::parse_font_attributes() {
	FontAttributes result{};

	for (;;) {
		switch (UTEXT_NEXT32(&m_iter)) {
			case 'c':
				parse_attribute(U"olor=\"", result.color);
				result.colorChange = true;
				break;
			case 'f':
				parse_font_face(result);
				break;
			case 's':
				parse_attribute(U"ize=\"", result.size);
				result.sizeChange = true;
				break;
			case ' ':
				break;
			case '>':
				return result;
			default:
				raise_error();
		}
	}

	return result;
}

void RichTextParser::parse_font_face(FontAttributes& attribs) {
	auto faceName = parse_attribute(U"ace=\"");
	if (faceName.empty()) {
		raise_error();
		return;
	}

	auto& cache = *m_ownedFonts[m_fontRuns.get_current_value()].get_font_cache();
	attribs.family = cache.get_font_family(faceName);

	if (attribs.family == FontCache::INVALID_FAMILY) {
		raise_error();
	}
}

void RichTextParser::parse_strikethrough() {
	m_strikethroughRuns.push(m_output.length(), true);
	parse_content(U"s>");
	m_strikethroughRuns.pop(m_output.length());
}

void RichTextParser::parse_underline() {
	m_underlineRuns.push(m_output.length(), true);
	parse_content(U"u>");
	m_underlineRuns.pop(m_output.length());
}

void RichTextParser::parse_stroke() {
	if (!consume_word(U"roke")) {
		raise_error();
		return;
	}

	auto state = parse_stroke_attributes();

	m_strokeRuns.push(m_output.length(), state); 

	parse_content(U"stroke>");

	m_strokeRuns.pop(m_output.length());
}

StrokeState RichTextParser::parse_stroke_attributes() {
	StrokeState result{
		.color = {0.f, 0.f, 0.f, 1.f}, 
		.thickness = 1,
		.joins = StrokeType::ROUND,
	};

	for (;;) {
		switch (UTEXT_NEXT32(&m_iter)) {
			case 'c':
			{
				Color c{};
				parse_attribute(U"olor=\"", c);
				result.color = {c.r, c.g, c.b, result.color.a};
			}
				break;
			case 'j':
				parse_stroke_joins(result);
				break;
			case 't':
				switch (UTEXT_NEXT32(&m_iter)) {
					case 'h':
						parse_attribute(U"ickness=\"", result.thickness);
						break;
					case 'r':
						parse_attribute(U"ansparency=\"", result.color.a);
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
		}
	}

	return result;
}

void RichTextParser::parse_stroke_joins(StrokeState& attribs) {
	if (!consume_word(U"oins=\"")) {
		return;
	}

	auto start = UTEXT_GETNATIVEINDEX(&m_iter);

	for (;;) {
		auto end = UTEXT_GETNATIVEINDEX(&m_iter);
		auto c = UTEXT_NEXT32(&m_iter);

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
		else if (c == U_SENTINEL) {
			raise_error();
			return;
		}
	}
}

std::string_view RichTextParser::parse_attribute(std::u32string_view name) {
	if (!consume_word(name)) {
		return {};
	}

	auto start = UTEXT_GETNATIVEINDEX(&m_iter);

	for (;;) {
		auto end = UTEXT_GETNATIVEINDEX(&m_iter);
		auto c = UTEXT_NEXT32(&m_iter);

		if (c == '"') {
			return std::string_view(m_text.data() + start, end - start);
		}
		else if (c == U_SENTINEL) {
			raise_error();
			return {};
		}
	}
}

template <typename T> requires std::is_arithmetic_v<T>
void RichTextParser::parse_attribute(std::u32string_view name, T& value) {
	auto attrib = parse_attribute(name);
	if (auto [ptr, ec] = std::from_chars(attrib.data(), attrib.data() + attrib.size(), value);
			ec != std::errc{}) {
		raise_error();
	}
}

void RichTextParser::parse_attribute(std::u32string_view name, Color& value) {
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

bool RichTextParser::parse_color(uint32_t& color) {
	switch (UTEXT_NEXT32(&m_iter)) {
		case '#':
			return parse_color_hex(color);
		case 'r':
			return parse_color_rgb(color);
		default:
			raise_error();
			return false;
	}
}

bool RichTextParser::parse_color_hex(uint32_t& color) {
	char buffer[6]{};

	for (size_t i = 0; i < 6; ++i) {
		auto c = UTEXT_NEXT32(&m_iter);

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

bool RichTextParser::parse_color_rgb(uint32_t& color) {
	static constexpr const char* stops = ",,)";

	if (!consume_word(U"gb(")) {
		return false;
	}

	uint8_t colorChannels[3]{};

	for (size_t channelIndex = 0; channelIndex < 3; ++channelIndex) {
		char numberBuffer[3]{};
		size_t i = 0;
		auto c = UTEXT_NEXT32(&m_iter);

		// Skip leading whitespace
		while (is_space(c)) {
			c = UTEXT_NEXT32(&m_iter);
		}

		for (;;) {
			if (c >= '0' && c <= '9' && i < 3) {
				numberBuffer[i] = c;
			}
			else if (is_space(c) || c == stops[channelIndex]) {
				std::from_chars(numberBuffer, numberBuffer + i + 1, colorChannels[channelIndex]);

				// Skip trailing whitespace
				while (c != stops[channelIndex]) {
					c = UTEXT_NEXT32(&m_iter);

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

			c = UTEXT_NEXT32(&m_iter);
			++i;
		}
	}

	color = (static_cast<uint32_t>(colorChannels[0]) << 16)
			| (static_cast<uint32_t>(colorChannels[1]) << 8)
			| static_cast<uint32_t>(colorChannels[2]);

	return true;
}

bool RichTextParser::consume_char(UChar32 c) {
	if (UTEXT_NEXT32(&m_iter) == c) {
		return true;
	}

	raise_error();
	return false;
}

bool RichTextParser::consume_word(std::u32string_view word) {
	for (size_t i = 0; i < word.size(); ++i) {
		if (!consume_char(static_cast<UChar32>(word[i]))) {
			return false;
		}
	}

	return true;
}

void RichTextParser::raise_error() {
	m_error = true;
}

void RichTextParser::finalize_runs() {
	m_fontRuns.pop(m_output.length());
	m_colorRuns.pop(m_output.length());
	m_strokeRuns.pop(m_output.length());
	m_strikethroughRuns.pop(m_output.length());
	m_underlineRuns.pop(m_output.length());
}

