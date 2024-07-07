#pragma once

#include <cstdint>

namespace Text {

enum class CursorAffinity : uint8_t {
	DEFAULT,
	OPPOSITE
};

struct CursorPosition {
	static constexpr const uint32_t AFFINITY_MASK = 1u << 31;
	static constexpr const uint32_t POSITION_MASK = ~AFFINITY_MASK;
	static constexpr const uint32_t INVALID_VALUE = ~0u;

	uint32_t data;

	constexpr void set_position(int32_t pos) {
		data = (data & AFFINITY_MASK) | (static_cast<uint32_t>(pos) & POSITION_MASK);
	}

	constexpr void set_affinity(CursorAffinity affinity) {
		if (affinity == CursorAffinity::OPPOSITE) {
			data |= (1u << 31);
		}
		else {
			data &= POSITION_MASK;
		}
	}

	constexpr void set_invalid() {
		data = INVALID_VALUE;
	}

	constexpr int32_t get_position() const {
		return static_cast<int32_t>(data & POSITION_MASK);
	}

	constexpr CursorAffinity get_affinity() const {
		return static_cast<CursorAffinity>(data >> 31);
	}

	constexpr bool is_valid() const {
		return data != INVALID_VALUE;
	}

	constexpr bool operator==(const CursorPosition& other) const {
		return data == other.data;
	}

	constexpr bool operator!=(const CursorPosition& other) const {
		return !(*this == other);
	}
};

}

