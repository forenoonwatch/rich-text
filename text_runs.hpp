#pragma once

#include <vector>

namespace RichText {

constexpr const uint32_t FLAG_BOOL_RUNS = 1u << 31;
constexpr const uint32_t MASK_BOOL_RUNS = ~(1u << 31);

template <typename T>
class TextRuns {
	public:
		constexpr TextRuns() = default;
		constexpr TextRuns(const T& value, int32_t limit)
				: m_values{value}
				, m_limits{limit} {}

		constexpr TextRuns(TextRuns&&) noexcept = default;
		constexpr TextRuns& operator=(TextRuns&&) noexcept = default;

		TextRuns(const TextRuns&) = delete;
		void operator=(const TextRuns&) = delete;

		template <typename... Args>
		constexpr void add(int32_t limit, Args&&... args) {
			m_values.emplace_back(std::forward<Args>(args)...);
			m_limits.emplace_back(limit);
		}

		constexpr const T& get_value(int32_t index) const {
			auto first = 0;
			auto count = m_limits.size();

			while (count > 0) {
				auto step = count / 2;
				auto i = first + step;

				if (m_limits[i] <= index) {
					first = i + 1;
					count -= step + 1;
				}
				else {
					count = step;
				}
			}

			return m_values[first];
		}

		constexpr bool empty() const {
			return m_values.empty();
		}

		constexpr size_t get_value_count() const {
			return m_values.size();
		}

		constexpr int32_t get_limit() const {
			return m_limits.back();
		}

		constexpr const T* get_values() const {
			return m_values.data();
		}

		constexpr const int32_t* get_limits() const {
			return m_limits.data();
		}
	private:
		std::vector<T> m_values;
		std::vector<int32_t> m_limits;
};

}

