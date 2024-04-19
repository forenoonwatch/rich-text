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
	FontWeight sourceWeight{FontWeight::REGULAR};
	FontStyle sourceStyle{FontStyle::NORMAL};

	constexpr bool operator==(const FontFace& other) const {
		return handle == other.handle;
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

}

