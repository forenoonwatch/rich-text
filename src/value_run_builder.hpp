#pragma once

#include "value_runs.hpp"

namespace Text {

template <typename T>
class ValueRunBuilder {
	public:
		template <typename U>
		constexpr explicit ValueRunBuilder(U&& baseValue)
				: m_stack{std::forward<U>(baseValue)} {}

		template <typename... Args>
		constexpr void push(int32_t limit, Args&&... args) {
			m_runs.add(limit, m_stack.back());
			m_stack.emplace_back(std::forward<Args>(args)...);
		}

		constexpr void pop(int32_t limit) {
			if (m_runs.empty() || m_runs.get_limit() < limit) {
				m_runs.add(limit, m_stack.back());
			}

			m_stack.pop_back();
		}

		constexpr ValueRuns<T> get() {
			return std::move(m_runs);
		}

		constexpr T& get_base_value() {
			return m_stack.front();
		}

		constexpr T& get_current_value() {
			return m_stack.back();
		}
	private:
		ValueRuns<T> m_runs;
		std::vector<T> m_stack;
};

}

