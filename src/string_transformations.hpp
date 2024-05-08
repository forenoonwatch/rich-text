#pragma once

#include "pair.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace Text {

/**
 * Converts a UTF-8 string to uppercase following Unicode standard case mapping. Also generates a
 * per-code unit index map, mapping the index of each code unit to the beginning of the corresponding
 * codepoint in the original string.
 */
Pair<std::string, std::vector<uint32_t>> string_to_upper(std::string_view str);

}

