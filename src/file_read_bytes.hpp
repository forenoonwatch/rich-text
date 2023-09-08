#pragma once

#include <vector>

#include <cstdio>
#include <cstddef>

inline std::vector<char> file_read_bytes(const char* fileName) {
	FILE* file = std::fopen(fileName, "rb");

	if (!file) {
		return {};
	}

	std::fseek(file, 0, SEEK_END);
	std::vector<char> result(std::ftell(file));
	std::rewind(file);

	std::fread(result.data(), 1, result.size(), file);
	std::fclose(file);

	return result;
}

