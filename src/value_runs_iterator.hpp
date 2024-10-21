#pragma once

#include <cstdint>
#include <utility>

namespace Text {

template <typename>
class ValueRuns;

template <typename T>
concept ValueRunsIterable = requires(T t) {
	{ t.get_limit() } -> std::same_as<int32_t>;
	t.get_value();
	t.advance_to(int32_t{});
};

template <typename T>
class ValueRunsIterator {
	public:
		constexpr explicit ValueRunsIterator(const ValueRuns<T>& runs)
				: m_runs(&runs) {}

		constexpr T get_value() const;
		constexpr int32_t get_limit() const;

		constexpr void advance_to(int32_t index);
	private:
		const ValueRuns<T>* m_runs;
		int32_t m_runIndex{};
};

template <typename T>
class MaybeDefaultRunsIterator {
	public:
		constexpr explicit MaybeDefaultRunsIterator(const ValueRuns<T>* pRuns, T defaultValue,
					int32_t defaultLimit)
				: m_runs(pRuns)
				, m_runIndex(pRuns ? 0 : defaultLimit)
				, m_defaultValue(std::move(defaultValue)) {}

		constexpr T get_value() const;
		constexpr int32_t get_limit() const;

		constexpr void advance_to(int32_t index);
	private:
		const ValueRuns<T>* m_runs;
		int32_t m_runIndex;
		T m_defaultValue;
};

// ValueRunsIterator

template <typename T>
constexpr T ValueRunsIterator<T>::get_value() const {
	return m_runs->get_run_value(m_runIndex);
}

template <typename T>
constexpr int32_t ValueRunsIterator<T>::get_limit() const {
	return m_runs->get_run_limit(m_runIndex); 
}

template <typename T>
constexpr void ValueRunsIterator<T>::advance_to(int32_t index) {
	while (m_runs->get_run_limit(m_runIndex) <= index) {
		++m_runIndex;
	}
}

// MaybeDefaultRunsIterator

template <typename T>
constexpr T MaybeDefaultRunsIterator<T>::get_value() const {
	return m_runs ? m_runs->get_run_value(m_runIndex) : m_defaultValue;
}

template <typename T>
constexpr int32_t MaybeDefaultRunsIterator<T>::get_limit() const {
	return m_runs ? m_runs->get_run_limit(m_runIndex) : m_runIndex;
}

template <typename T>
constexpr void MaybeDefaultRunsIterator<T>::advance_to(int32_t index) {
	while (m_runs && m_runs->get_run_limit(m_runIndex) <= index) {
		++m_runIndex;
	}
}

}

