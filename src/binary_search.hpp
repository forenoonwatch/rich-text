#pragma once

#include <cstddef>

template <typename Condition>
constexpr size_t binary_search(size_t first, size_t count, Condition&& cond) {
	while (count > 0) {
		auto step = count / 2;
		auto i = first + step;

		if (cond(i)) {
			first = i + 1;
			count -= step + 1;
		}
		else {
			count = step;
		}
	}

	return first;
}

