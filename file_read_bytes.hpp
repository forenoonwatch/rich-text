#pragma once

#include <vector>

#include <cstdio>
#include <cstddef>

inline std::vector<char> file_read_bytes(const char* fileName) {
	FILE* file{};

	if (fopen_s(&file, fileName, "rb") != 0) {
		return {};
	}

	std::fseek(file, 0, SEEK_END);
	std::vector<char> result(std::ftell(file));
	std::rewind(file);

	std::fread(result.data(), 1, result.size(), file);
	std::fclose(file);

	return result;
}

