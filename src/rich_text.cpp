#include "rich_text.hpp"

#include "font.hpp"
#include "font_cache.hpp"
#include "multi_script_font.hpp"
#include "text_run_builder.hpp"

#include <unicode/utext.h>

#include <charconv>
#include <string_view>

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
		explicit RichTextParser(const std::string& text, const MultiScriptFont& baseFont, Color&& baseColor);

		void parse();

		RichText::Result get_result(std::string& contentText);
		bool has_error() const;
	private:
		UText m_iter UTEXT_INITIALIZER;
		icu::UnicodeString m_output;
		int32_t m_charIndex{};
		bool m_error{false};

		const std::string& m_text;
		
		TextRunBuilder<uint32_t> m_fontRuns;
		TextRunBuilder<Color> m_colorRuns;
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
		void parse_font_end();

		void parse_font_color(FontAttributes&);
		void parse_font_size(FontAttributes&);
		void parse_font_face(FontAttributes&);

		void parse_strikethrough();
		void parse_underline();

		void parse_italic();
		void parse_italic_end();

		void parse_stroke();
		void parse_stroke_end();

		void parse_stroke_attributes();
		void parse_stroke_color();
		void parse_stroke_joins();
		void parse_stroke_t_attributes();
		void parse_stroke_thickness();
		void parse_stroke_transparency();

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
		const MultiScriptFont& baseFont, Color baseColor) {
	contentText = text;
	auto str = icu::UnicodeString::fromUTF8(text);
	auto length = str.length();

	return {
		.str = std::move(str),
		.fontRuns{&baseFont, length},
		.colorRuns{std::move(baseColor), length},
		.strikethroughRuns{false, length},
		.underlineRuns{false, length},
	};
}

RichText::Result RichText::parse(const std::string& text, std::string& contentText,
		const MultiScriptFont& baseFont, Color baseColor) {
	RichTextParser parser(text, baseFont, std::move(baseColor));
	parser.parse();
	return parser.get_result(contentText);
}

// RichTextParser

RichTextParser::RichTextParser(const std::string& text, const MultiScriptFont& baseFont, Color&& baseColor)
		: m_text(text)
		, m_fontRuns{0u}
		, m_colorRuns{std::move(baseColor)}
		, m_strikethroughRuns{false}
		, m_underlineRuns{false}
		, m_ownedFonts{baseFont} {
	UErrorCode errc{};
	utext_openUTF8(&m_iter, text.data(), text.size(), &errc);
}

RichText::Result RichTextParser::get_result(std::string& contentText) {
	if (m_error) {
		return RichText::make_default_runs(m_text, contentText, m_ownedFonts.front(),
				m_colorRuns.get_base_value());
	}
	else {
		m_output.toUTF8String(contentText);
		auto fontIndexRuns = m_fontRuns.get();

		RichText::Result result{
			.str = std::move(m_output),
			.fontRuns{fontIndexRuns.get_value_count()},
			.colorRuns = m_colorRuns.get(),
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
			++m_charIndex;
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
			// FIXME: parse_stroke();
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

		m_fontRuns.push(m_charIndex, newFontIndex);
	}

	if (fontAttribs.colorChange) {
		m_colorRuns.push(m_charIndex, fontAttribs.color); 
	}

	parse_content(U"font>");

	if (hasFontChange) {
		m_fontRuns.pop(m_charIndex);
	}

	if (fontAttribs.colorChange) {
		m_colorRuns.pop(m_charIndex);
	}
}

FontAttributes RichTextParser::parse_font_attributes() {
	FontAttributes result{};

	for (;;) {
		switch (UTEXT_NEXT32(&m_iter)) {
			case 'c':
				parse_font_color(result);
				break;
			case 'f':
				parse_font_face(result);
				break;
			case 's':
				parse_font_size(result);
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

void RichTextParser::parse_font_color(FontAttributes& attribs) {
	if (!consume_word(U"olor=\"")) {
		return;
	}

	uint32_t color{};
	if (!parse_color(color)) {
		return;
	}

	if (!consume_char('"')) {
		return;
	}

	attribs.color = Color::from_rgb_uint(color);
	attribs.colorChange = true;
}

void RichTextParser::parse_font_face(FontAttributes& attribs) {
	if (!consume_word(U"ace=\"")) {
		return;
	}

	auto start = UTEXT_GETNATIVEINDEX(&m_iter);

	for (;;) {
		auto end = UTEXT_GETNATIVEINDEX(&m_iter);
		auto c = UTEXT_NEXT32(&m_iter);

		if (c == '"') {
			auto& cache = *m_ownedFonts[m_fontRuns.get_current_value()].get_font_cache();
			attribs.family = cache.get_font_family(std::string_view(m_text.data() + start, end - start));

			if (attribs.family == FontCache::INVALID_FAMILY) {
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

void RichTextParser::parse_font_size(FontAttributes& attribs) {
	if (!consume_word(U"ize=\"")) {
		return;
	}

	// Max decimal digits that can represent a 32 bit uint: 10
	char buffer[10]{};
	size_t i = 0;

	for (;;) {
		auto c = UTEXT_NEXT32(&m_iter);

		if (c >= '0' && c <= '9' && i < 10) {
			buffer[i++] = c;
		}
		else if (c == '"') {
			std::from_chars(buffer, buffer + i, attribs.size);
			attribs.sizeChange = true;
			return;
		}
		else {
			raise_error();
			return;
		}
	}
}

void RichTextParser::parse_strikethrough() {
	m_strikethroughRuns.push(m_charIndex, true);
	parse_content(U"s>");
	m_strikethroughRuns.pop(m_charIndex);
}

void RichTextParser::parse_underline() {
	m_underlineRuns.push(m_charIndex, true);
	parse_content(U"u>");
	m_underlineRuns.pop(m_charIndex);
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
	m_fontRuns.pop(m_charIndex);
	m_colorRuns.pop(m_charIndex);
	m_strikethroughRuns.pop(m_charIndex);
	m_underlineRuns.pop(m_charIndex);
}

