#pragma once

#include "value_runs_iterator.hpp"

#include <algorithm>
#include <tuple>

namespace Text {

template <typename Functor, typename... Runs>
constexpr void iterate_run_intersections(Functor&& func, Runs&&... runs);

namespace Internal {

template <typename Head, typename... Tail>
constexpr int32_t find_next_min_limit(int32_t minLimit, const size_t* indices, Head&& head, Tail&&... tail) {
	if constexpr (sizeof...(Tail) > 0) {
		return std::min(head.get_run_limit(*indices), find_next_min_limit(minLimit, indices + 1, tail...));
	}
	else {
		return head.get_run_limit(*indices);
	}
}

template <typename Head, typename... Tail>
constexpr void advance_intersect_indices(int32_t minLimit, size_t* indices, Head&& head, Tail&&... tail) {
	*indices += head.get_run_limit(*indices) <= minLimit;

	if constexpr (sizeof...(Tail) > 0) {
		advance_intersect_indices(minLimit, indices + 1, tail...);
	}
}

template <size_t N, typename Tuple>
constexpr decltype(auto) get_run_value(Tuple&& t, const size_t* indices) {
	return std::get<N>(t)->get_run_value(indices[N]);
}

template <typename Functor, typename Tuple, size_t... Indices>
constexpr void call_intersect_func(Functor&& func, Tuple&& tup, const size_t* indices, int32_t minLimit,
		std::index_sequence<Indices...>) {
	func(minLimit, get_run_value<Indices>(tup, indices)...);
}

}

template <typename Functor, typename... Runs>
constexpr void iterate_run_intersections(Functor&& func, Runs&&... runs) {
	static_assert(sizeof...(Runs) > 0);
	int32_t minLimit{};
	auto maxLimit = std::max<int32_t>({runs.get_limit()...});
	size_t indices[sizeof...(Runs)]{};
	auto tup = std::make_tuple(&runs...);

	while (minLimit < maxLimit) {
		minLimit = Internal::find_next_min_limit(minLimit, indices, runs...);
		Internal::call_intersect_func(func, tup, indices, minLimit, std::make_index_sequence<sizeof...(Runs)>{});
		Internal::advance_intersect_indices(minLimit, indices, runs...);
	}
}

template <typename Functor, ValueRunsIterable... Iterators>
constexpr void iterate_run_intersections(int32_t minLimit, int32_t maxLimit, Functor&& func,
		Iterators&&... iterators) {
	while (minLimit < maxLimit) {
		minLimit = std::min<int32_t>({iterators.get_limit()...});
		func(minLimit, iterators.get_value()...);
		(iterators.advance_to(minLimit), ...);
	}
}

}

