#pragma once

#include <cstdint>

#include <type_traits>
#include <vector>

namespace Text {

template <typename>
class ValueRuns;

namespace Internal {

template <typename Derived>
class ValueRunsMixin {
	public:
		constexpr size_t get_run_count() const {
			return static_cast<const Derived*>(this)->get_run_count();
		}

		constexpr int32_t get_run_limit(size_t runIndex) const {
			return static_cast<const Derived*>(this)->get_run_limit(runIndex);
		}

		template <typename Functor>
		constexpr void for_each_run_in_range(int32_t offset, int32_t length, Functor&& func) const {
			size_t i = 0;

			while (i < get_run_count() && get_run_limit(i) <= offset) {
				++i;
			}

			for (; i < get_run_count(); ++i) {
				auto newLimit = get_run_limit(i) - offset;

				if (newLimit < length) {
					func(newLimit, static_cast<const Derived*>(this)->get_run_value(i));
				}
				else {
					func(length, static_cast<const Derived*>(this)->get_run_value(i));
					break;
				}
			}
		}

		constexpr void get_runs_subset(int32_t offset, int32_t length, Derived& output) const {
			for_each_run_in_range(offset, length, [&](int32_t limit, auto value) {
				output.add(limit, value);
			});
		}

		constexpr size_t get_run_containing_index(int32_t index) const {
			size_t first{};
			auto count = get_run_count();

			while (count > 0) {
				auto step = count / 2;
				auto i = first + step;

				if (get_run_limit(i) <= index) {
					first = i + 1;
					count -= step + 1;
				}
				else {
					count = step;
				}
			}

			return first;
		}

		constexpr decltype(auto) get_value(int32_t index) const {
			return static_cast<const Derived*>(this)->get_run_value(get_run_containing_index(index));
		}
};

}

template <typename T> requires(!std::is_same_v<T, bool>)
class ValueRuns<T> : public Internal::ValueRunsMixin<ValueRuns<T>> {
	public:
		using value_type = T;

		constexpr ValueRuns() = default;
		template <typename U>
		constexpr ValueRuns(U&& value, int32_t limit)
				: m_values{std::forward<U>(value)}
				, m_limits{limit} {}
		constexpr ValueRuns(size_t initialCapacity) {
			m_values.reserve(initialCapacity);
			m_limits.reserve(initialCapacity);
		}

		constexpr ValueRuns(ValueRuns&&) noexcept = default;
		constexpr ValueRuns& operator=(ValueRuns&&) noexcept = default;

		ValueRuns(const ValueRuns&) = delete;
		void operator=(const ValueRuns&) = delete;

		template <typename... Args>
		constexpr void add(int32_t limit, Args&&... args) {
			m_values.emplace_back(std::forward<Args>(args)...);
			m_limits.emplace_back(limit);
		}

		constexpr T get_run_value(size_t runIndex) const {
			return m_values[runIndex];
		}

		constexpr int32_t get_run_limit(size_t runIndex) const {
			return m_limits[runIndex];
		}	

		constexpr void clear() {
			m_values.clear();
			m_limits.clear();
		}

		constexpr bool empty() const {
			return m_values.empty();
		}

		constexpr size_t get_run_count() const {
			return m_limits.size();
		}

		constexpr int32_t get_limit() const {
			return m_limits.back();
		}
	private:
		std::vector<T> m_values;
		std::vector<int32_t> m_limits;
};

template <typename T> requires(std::is_same_v<T, bool>)
class ValueRuns<T> : public Internal::ValueRunsMixin<ValueRuns<T>> {
	private:
		static constexpr const uint32_t MASK_VALUE = ~(1u << 31);
	public:
		using value_type = T;

		constexpr ValueRuns() = default;
		constexpr ValueRuns(bool value, int32_t limit)
				: m_values{encode(value, limit)} {}
		constexpr ValueRuns(size_t initialCapacity) {
			m_values.reserve(initialCapacity);
		}

		constexpr ValueRuns(ValueRuns&&) noexcept = default;
		constexpr ValueRuns& operator=(ValueRuns&&) noexcept = default;

		ValueRuns(const ValueRuns&) = delete;
		void operator=(const ValueRuns&) = delete;

		constexpr void add(int32_t limit, bool value) {
			m_values.emplace_back(encode(value, limit));
		}

		constexpr bool get_run_value(size_t runIndex) const {
			return static_cast<bool>(m_values[runIndex] >> 31);
		}

		constexpr int32_t get_run_limit(size_t runIndex) const {
			return static_cast<int32_t>(m_values[runIndex] & MASK_VALUE);
		}	

		constexpr void clear() {
			m_values.clear();
		}

		constexpr bool empty() const {
			return m_values.empty();
		}

		constexpr size_t get_run_count() const {
			return m_values.size();
		}

		constexpr int32_t get_limit() const {
			return static_cast<int32_t>(m_values.back() & MASK_VALUE);
		}
	private:
		std::vector<uint32_t> m_values;

		static constexpr uint32_t encode(bool value, int32_t limit) {
			return (static_cast<uint32_t>(value) << 31) | static_cast<uint32_t>(limit);
		}
};

}

