#pragma once

#include <string_view>

namespace Text {

/**
 * A handle to a read-only file mapped into memory. `mapping` is a pointer to the mapped memory,
 * and `handle` is an internal value for use by the mapping interface. Users should not modify `handle`.
 */
struct FileMapping {
	const void* mapping;
	size_t size;
	void* handle;
};

struct FileMappingFunctions {
	FileMapping (*pfnMapFile)(std::string_view fileName);
	void (*pfnUnmapFile)(const FileMapping& mapping);
};

/**
 * @brief Default implementation for mapping a file into memory. The result's `mapping` is `nullptr` if
 * mapping failed.
 */
[[nodiscard]] FileMapping map_file_default(std::string_view fileName);
/**
 * @brief Default implementation for unmapping a mapped file. Behavior when supplying invalid `FileMapping`
 * objects is undefined.
 */
void unmap_file_default(const FileMapping& mapping);

}

