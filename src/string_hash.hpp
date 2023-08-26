#pragma once

#include <cstddef>

#include <string>
#include <string_view>

struct StringHash {
	using is_transparent = void;

	[[nodiscard]] size_t operator()(const char* str) const {
		return std::hash<std::string_view>{}(str);
	}

	[[nodiscard]] size_t operator()(std::string_view str) const {
		return std::hash<std::string_view>{}(str);
	}

	[[nodiscard]] size_t operator()(const std::string& str) const {
		return std::hash<std::string>{}(str);
	}
};

