#pragma once

#include <memory>

#include <cstdio>
#include <cstddef>

inline std::unique_ptr<char[]> file_read_bytes(const char* fileName, size_t& outFileSize) {
	FILE* file = std::fopen(fileName, "rb");
	if (!file) {
		return {};
	}

	std::fseek(file, 0, SEEK_END);
	outFileSize = std::ftell(file);
	std::rewind(file);

	auto result = std::make_unique<char[]>(outFileSize);
	std::fread(result.get(), 1, outFileSize, file);

	std::fclose(file);

	return result;
}

