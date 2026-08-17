#pragma once
#include <concepts>
#include <optional>
#include <string>
#include <string_view>
#include <cstdint>
#include <vector>
#include <chrono>

#include "cpplinq/mapping/data_types.h"

namespace cpplinq {

// SQL type enumeration
enum class SqlType {
    Integer,
    BigInt,
    UnsignedBigInt,
    Real,
    Decimal,
    Text,
    Blob,
    Boolean,
    Date,
    Time,
    Timestamp,
    Interval
};

// Primary trait: map C++ type -> SQL type
template <typename T>
struct sql_type_of;

// Signed integer mappings
template <> struct sql_type_of<int8_t>      { static constexpr SqlType value = SqlType::Integer; };
template <> struct sql_type_of<int16_t>     { static constexpr SqlType value = SqlType::Integer; };
template <> struct sql_type_of<int32_t>     { static constexpr SqlType value = SqlType::Integer; };
#if !defined(_MSC_VER) || defined(_NATIVE_WCHAR_T_DEFINED)
template <> struct sql_type_of<long>        { static constexpr SqlType value = (sizeof(long) == 4 ? SqlType::Integer : SqlType::BigInt); };
#endif
template <> struct sql_type_of<int64_t>     { static constexpr SqlType value = SqlType::BigInt; };

// Unsigned integer mappings
template <> struct sql_type_of<uint8_t>     { static constexpr SqlType value = SqlType::UnsignedBigInt; };
template <> struct sql_type_of<uint16_t>    { static constexpr SqlType value = SqlType::UnsignedBigInt; };
template <> struct sql_type_of<uint32_t>    { static constexpr SqlType value = SqlType::UnsignedBigInt; };
#if !defined(_MSC_VER) || defined(_NATIVE_WCHAR_T_DEFINED)
template <> struct sql_type_of<unsigned long> { static constexpr SqlType value = SqlType::UnsignedBigInt; };
#endif
template <> struct sql_type_of<uint64_t>    { static constexpr SqlType value = SqlType::UnsignedBigInt; };

// Floating-point mappings
template <> struct sql_type_of<float>       { static constexpr SqlType value = SqlType::Real; };
template <> struct sql_type_of<double>      { static constexpr SqlType value = SqlType::Real; };

// Decimal mapping
template <> struct sql_type_of<SqlNumeric>  { static constexpr SqlType value = SqlType::Decimal; };

// Text, Boolean, Blob mappings
template <> struct sql_type_of<std::string> { static constexpr SqlType value = SqlType::Text; };
template <> struct sql_type_of<bool>        { static constexpr SqlType value = SqlType::Boolean; };
template <> struct sql_type_of<std::vector<uint8_t>> { static constexpr SqlType value = SqlType::Blob; };

// Date / Time mappings
template <> struct sql_type_of<SqlDate>      { static constexpr SqlType value = SqlType::Date; };
template <> struct sql_type_of<SqlTime>      { static constexpr SqlType value = SqlType::Time; };
template <> struct sql_type_of<SqlTimestamp> { static constexpr SqlType value = SqlType::Timestamp; };
template <> struct sql_type_of<std::chrono::system_clock::time_point> { static constexpr SqlType value = SqlType::Timestamp; };

// Interval mappings
template <> struct sql_type_of<SqlInterval>  { static constexpr SqlType value = SqlType::Interval; };
template <typename Rep, typename Period>
struct sql_type_of<std::chrono::duration<Rep, Period>> { static constexpr SqlType value = SqlType::Interval; };

// Optional<T> -> same SQL type as T, but nullable
template <typename T>
struct sql_type_of<std::optional<T>> : sql_type_of<T> {};

// Concept: types that can be stored in a database column
template <typename T>
concept SqlMappable = requires {
    { sql_type_of<std::remove_cvref_t<T>>::value } -> std::convertible_to<SqlType>;
};

// Detect nullable (optional) columns
template <typename T>
struct is_nullable : std::false_type {};

template <typename T>
struct is_nullable<std::optional<T>> : std::true_type {};

template <typename T>
inline constexpr bool is_nullable_v = is_nullable<T>::value;

} // namespace cpplinq
