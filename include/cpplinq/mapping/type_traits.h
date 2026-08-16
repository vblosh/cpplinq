#pragma once
#include <concepts>
#include <optional>
#include <string>
#include <string_view>
#include <cstdint>
#include <vector>

namespace cpplinq {

// SQL type enumeration
enum class SqlType {
    Integer,
    BigInt,
    Real,
    Text,
    Blob,
    Boolean
};

// Primary trait: map C++ type -> SQL type
template <typename T>
struct sql_type_of;

template <> struct sql_type_of<int>         { static constexpr SqlType value = SqlType::Integer; };
template <> struct sql_type_of<int64_t>     { static constexpr SqlType value = SqlType::BigInt; };
template <> struct sql_type_of<double>      { static constexpr SqlType value = SqlType::Real; };
template <> struct sql_type_of<float>       { static constexpr SqlType value = SqlType::Real; };
template <> struct sql_type_of<std::string> { static constexpr SqlType value = SqlType::Text; };
template <> struct sql_type_of<bool>        { static constexpr SqlType value = SqlType::Boolean; };
template <> struct sql_type_of<std::vector<uint8_t>> { static constexpr SqlType value = SqlType::Blob; };

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
