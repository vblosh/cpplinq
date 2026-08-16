#pragma once
#include "cpplinq/mapping/type_traits.h"
#include <string_view>
#include <type_traits>

namespace cpplinq {

// Column constraint tags
struct PrimaryKeyTag {};
struct AutoIncrementTag {};
struct NotNullTag {};
struct UniqueTag {};

inline constexpr PrimaryKeyTag     primary_key{};
inline constexpr AutoIncrementTag  auto_increment{};
inline constexpr NotNullTag        not_null{};
inline constexpr UniqueTag         unique_col{};

// Compile-time fixed string for template parameters (C++20 NTTP)
template <size_t N>
struct FixedString {
    char data[N]{};
    constexpr FixedString(const char (&str)[N]) {
        for (size_t i = 0; i < N; ++i) data[i] = str[i];
    }
    constexpr std::string_view view() const { return {data, N - 1}; }
    constexpr operator std::string_view() const { return view(); }
};

// Column descriptor
template <typename Entity, typename FieldType>
struct ColumnDef {
    std::string_view  name;
    FieldType Entity::* member_ptr;
    bool is_primary_key    = false;
    bool is_auto_increment = false;
    bool is_not_null       = false;
    bool is_unique         = false;

    // The underlying SQL-mappable type (unwrap optional)
    using entity_type = Entity;
    using value_type = std::remove_cvref_t<FieldType>;
    static constexpr bool nullable = is_nullable_v<value_type>;
};

// Column factory function with variadic constraint tags
template <typename Entity, typename FieldType, typename... Constraints>
constexpr auto column(const char* name, FieldType Entity::* ptr, Constraints...) {
    ColumnDef<Entity, FieldType> col{name, ptr};
    auto apply = [&col](auto constraint) {
        using C = decltype(constraint);
        if constexpr (std::is_same_v<C, PrimaryKeyTag>)    col.is_primary_key = true;
        if constexpr (std::is_same_v<C, AutoIncrementTag>) col.is_auto_increment = true;
        if constexpr (std::is_same_v<C, NotNullTag>)       col.is_not_null = true;
        if constexpr (std::is_same_v<C, UniqueTag>)        col.is_unique = true;
    };
    (apply(Constraints{}), ...);
    return col;
}

} // namespace cpplinq
