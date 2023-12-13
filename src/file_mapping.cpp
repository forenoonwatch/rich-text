#include "file_mapping.hpp"

#include "common.hpp"

using namespace Text;

#if defined(RICHTEXT_OPERATING_SYSTEM_WINDOWS)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstring>
#include <string>

namespace {

struct FileData {
	HANDLE hFile;
	HANDLE hFileView;
};

}

FileMapping Text::map_file_default(std::string_view fileName) {
	size_t nameLength = MultiByteToWideChar(CP_UTF8, 0, fileName.data(), static_cast<int>(fileName.size()),
			nullptr, 0);
	std::wstring fileNameWide(nameLength, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, fileName.data(), static_cast<int>(fileName.size()), fileNameWide.data(),
			nameLength);
	HANDLE hFile = CreateFileW(fileNameWide.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
			FILE_FLAG_RANDOM_ACCESS, nullptr);

	if (hFile == INVALID_HANDLE_VALUE) {
		return {};
	}

	HANDLE hFileView = CreateFileMappingW(hFile, nullptr, PAGE_READONLY, 0, 0, nullptr);

	if (hFileView == INVALID_HANDLE_VALUE) {
		CloseHandle(hFile);
		return {};
	}

	const void* mapping = MapViewOfFile(hFileView, FILE_MAP_READ, 0, 0, 0);

	if (!mapping) {
		CloseHandle(hFile);
		CloseHandle(hFileView);
		return {};
	}

	size_t fileSize = GetFileSize(hFile, nullptr);
	return {mapping, fileSize, new FileData{hFile, hFileView}};
}

void Text::unmap_file_default(const FileMapping& mapping) {
	auto* pFileData = reinterpret_cast<FileData*>(mapping.handle);

	UnmapViewOfFile(mapping.mapping);
	CloseHandle(pFileData->hFileView);
	CloseHandle(pFileData->hFile);

	delete pFileData;
}

#elif defined(RICHTEXT_OPERATING_SYSTEM_LINUX)

#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include <string>

FileMapping Text::map_file_default(std::string_view fileName) {
	int fd = open(std::string(fileName).c_str(), O_RDONLY);
	if (fd == -1) {
		return {};
	}

	struct stat sb;
	if (fstat(fd, &sb) == -1) {
		return {};
	}

	size_t fileSize = static_cast<size_t>(sb.st_size);

	void* mapping = mmap(nullptr, fileSize, PROT_READ, MAP_PRIVATE, fd, 0);
	if (mapping == MAP_FAILED) {
		return {};
	}

	FileMapping result{mapping, fileSize};
	std::memcpy(&result.handle, &fd, sizeof(int));
	return result;
}

void Text::unmap_file_default(const FileMapping& mapping) {
	munmap(mapping.mapping, mapping.size);

	int fd;
	std::memcpy(&fd, &mapping.handle, sizeof(int));
	close(fd);
}

#else

#include <cstdio>
#include <cstdlib>

#include <string>

FileMapping Text::map_file_default(std::string_view fileName) {
	FILE* file = fopen(std::string(fileName).c_str(), "rb");

	if (!file) {
		return {};
	}

	fseek(file, 0, SEEK_END);
	size_t fileSize = ftell(file);
	rewind(file);
	
	auto* mapping = std::malloc(fileSize);

	if (!mapping) {
		fclose(file);
		return {};
	}

	fread(mapping, fileSize, 1, file);
	fclose(file);

	return {mapping, fileSize, nullptr};
}

void Text::unmap_file_default(const FileMapping& mapping) {
	std::free(const_cast<void*>(mapping.mapping));
}

#endif

