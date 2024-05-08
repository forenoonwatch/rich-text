#pragma once

#include <cstdint>

namespace Text {

enum class FontStyle : uint8_t {
	NORMAL = 0,
	ITALIC = 1,
	COUNT
};

enum class FontWeight : uint8_t {
	THIN,
	EXTRA_LIGHT,
	LIGHT,
	REGULAR,
	MEDIUM,
	SEMI_BOLD,
	BOLD,
	EXTRA_BOLD,
	BLACK,
	COUNT
};

using FamilyIndex_T = uint16_t;
using FaceIndex_T = uint16_t;

struct FontFamily {
	static constexpr const FamilyIndex_T INVALID_FAMILY = static_cast<FamilyIndex_T>(~0u);

	FamilyIndex_T handle{INVALID_FAMILY};

	constexpr bool operator==(const FontFamily& other) const {
		return handle == other.handle;
	}

	constexpr bool operator!=(const FontFamily& other) const {
		return !(*this == other);
	}

	constexpr bool valid() const {
		return handle != INVALID_FAMILY;
	}

	constexpr explicit operator bool() const {
		return valid();
	}
};

struct FontFace {
	static constexpr const FaceIndex_T INVALID_FACE = static_cast<FaceIndex_T>(~0u);

	FaceIndex_T handle{INVALID_FACE};
	// The source weight and style as this face expects its underlying font data to be, for use in calculating
	// the expected synthetic bold/italic transformations
	FontWeight sourceWeight: 4 {FontWeight::REGULAR};
	FontStyle sourceStyle: 2 {FontStyle::NORMAL};

	constexpr bool operator==(const FontFace& other) const {
		return handle == other.handle && sourceWeight == other.sourceWeight && sourceStyle == other.sourceStyle;
	}

	constexpr bool operator!=(const FontFace& other) const {
		return !(*this == other);
	}

	constexpr bool valid() const {
		return handle != INVALID_FACE;
	}

	constexpr explicit operator bool() const {
		return valid();
	}
};

struct SyntheticFontInfo {
	FontWeight srcWeight: 4;
	FontWeight dstWeight: 4;
	FontStyle srcStyle: 2;
	FontStyle dstStyle: 2;
	bool syntheticSubscript: 1;
	bool syntheticSuperscript: 1;
	bool syntheticSmallCaps: 1;
};

constexpr const float GLYPH_SUB_SUPER_SCALE = 0.7f;
constexpr const float GLYPH_SMALL_CAPS_SCALE = 0.8f;

// Based on fixed offset values used within WebKit
constexpr const float SUBSCRIPT_OFFSET_RATIO = 0.2f;
constexpr const float SUPERSCRIPT_OFFSET_RATIO = 0.34f;


constexpr float calc_font_scale_modifier(bool syntheticSmallCaps, bool syntheticSubSuper) {
	float sizeModifier = 1.f;
	sizeModifier *= syntheticSubSuper ? GLYPH_SUB_SUPER_SCALE : 1.f;
	sizeModifier *= syntheticSmallCaps ? GLYPH_SMALL_CAPS_SCALE : 1.f;
	return sizeModifier;
}

constexpr uint32_t calc_effective_font_size(uint32_t baseSize, bool syntheticSmallCaps,
		bool syntheticSubSuper) {
	return baseSize * calc_font_scale_modifier(syntheticSmallCaps, syntheticSubSuper);
}

constexpr float calc_baseline_offset(float baseSize, bool syntheticSmallCaps, bool syntheticSubscript,
		bool syntheticSuperscript) {
	auto baselineOffset = syntheticSubscript * SUBSCRIPT_OFFSET_RATIO
			+ syntheticSuperscript * -SUPERSCRIPT_OFFSET_RATIO;
	return baselineOffset * baseSize;
}

}

