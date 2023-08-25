#pragma once

#include <cstdint>

enum class FontFaceStyle : uint8_t {
	NORMAL = 0,
	ITALIC = 1,
	COUNT
};

enum class FontWeight : uint16_t {
	THIN = 100,
	EXTRA_LIGHT = 200,
	LIGHT = 300,
	REGULAR = 400,
	MEDIUM = 500,
	SEMI_BOLD = 600,
	BOLD = 700,
	EXTRA_BOLD = 800,
	BLACK = 900,
};

enum class FontWeightIndex : uint8_t {
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

