#pragma once

#include "font_common.hpp"

namespace Text {

class Font final {
	private:
		static constexpr const uint32_t INVALID_FONT = static_cast<uint32_t>(~0u);
	public:
		constexpr Font() = default;
		constexpr explicit Font(FontFamily family, FontWeight weight, FontStyle style,
					uint32_t size)
				: m_handle((family.handle << 16) | (static_cast<uint32_t>(weight) << 1)
						| static_cast<uint32_t>(style))
				, m_size(size) {}

		constexpr Font(Font&&) noexcept = default;
		constexpr Font& operator=(Font&&) noexcept = default;

		constexpr Font(const Font&) noexcept = default;
		constexpr Font& operator=(const Font&) noexcept = default;

		constexpr FontFamily get_family() const {
			return {static_cast<FamilyIndex_T>(m_handle >> 16)};
		}

		constexpr FontWeight get_weight() const {
			return static_cast<FontWeight>((m_handle >> 1) & 0xF);
		}

		constexpr FontStyle get_style() const {
			return static_cast<FontStyle>(m_handle & 1);
		}

		constexpr uint32_t get_size() const {
			return m_size;
		}

		constexpr bool valid() const {
			return m_handle != INVALID_FONT;
		}

		constexpr operator bool() const {
			return valid();
		}
	private:
		uint32_t m_handle{INVALID_FONT};
		uint32_t m_size{};
};

struct SingleScriptFont {
	FaceDataHandle face;
	uint32_t size;
	FontWeight weight: 4;
	FontStyle style: 2;
	bool subscript: 1;
	bool superscript: 1;
	bool smallcaps: 1;
	bool syntheticSubscript: 1;
	bool syntheticSuperscript: 1;
	bool syntheticSmallCaps: 1;

	constexpr uint32_t get_effective_size() const {
		return calc_effective_font_size(size, syntheticSmallCaps, syntheticSubscript || syntheticSuperscript);
	}

	constexpr float get_baseline_offset() const {
		return calc_baseline_offset(static_cast<float>(size), syntheticSmallCaps, syntheticSubscript,
				syntheticSuperscript);
	}

	constexpr bool operator==(const SingleScriptFont& other) const {
		return face == other.face && size == other.size;
	}
};

}

