#pragma once

#define ZN_DEFINE_UNARY_ENUM_OPERATOR(T, op)													\
	constexpr T operator op(const T& a) noexcept {												\
		static_assert(std::is_enum_v<T>);														\
		return static_cast<T>(op static_cast<std::underlying_type_t<T>>(a));					\
	}

#define ZN_DEFINE_BINARY_ENUM_OPERATOR(T1, T2, op)												\
	constexpr T1 operator op(const T1& a, const T2& b) noexcept {								\
		static_assert(std::is_enum_v<T1> && std::is_enum_v<T2>);								\
		return static_cast<T1>(static_cast<std::underlying_type_t<T1>>(a)						\
				op static_cast<std::underlying_type_t<T2>>(b));									\
	}

#define ZN_DEFINE_ASSIGNMENT_ENUM_OPERATOR(T1, T2, op)											\
	constexpr T1 operator op##=(T1& a, const T2& b) noexcept {									\
		static_assert(std::is_enum_v<T1> && std::is_enum_v<T2>);								\
		return a = static_cast<T1>(static_cast<std::underlying_type_t<T1>>(a)					\
				op static_cast<std::underlying_type_t<T2>>(b));									\
	}

#define ZN_DEFINE_ENUM_BITFLAG_OPERATORS(Enum)													\
	ZN_DEFINE_UNARY_ENUM_OPERATOR(Enum, ~)														\
	ZN_DEFINE_BINARY_ENUM_OPERATOR(Enum, Enum, |)												\
	ZN_DEFINE_BINARY_ENUM_OPERATOR(Enum, Enum, &)												\
	ZN_DEFINE_ASSIGNMENT_ENUM_OPERATOR(Enum, Enum, |)											\
	ZN_DEFINE_ASSIGNMENT_ENUM_OPERATOR(Enum, Enum, &)											\
