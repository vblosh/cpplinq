#pragma once
#include "cpplinq/core/column.h"
#include "cpplinq/core/expression.h"
#include <tuple>
#include <string_view>
#include <string>
#include <vector>

namespace cpplinq {

template <typename Entity, typename... Columns>
struct TableDef {
    using entity_type = Entity;

    std::string_view name;
    std::tuple<Columns...> columns;

    static constexpr size_t column_count = sizeof...(Columns);

    // Access a column by index
    template <size_t I>
    constexpr const auto& get_column() const { return std::get<I>(columns); }

    // operator[] for string-based column lookup (returns a ColumnHandle for expressions)
    expr::ColumnHandle operator[](const char* col_name) const {
        return expr::ColumnHandle(std::string(name), std::string(col_name));
    }

    // Get column names as vector
    std::vector<std::string> column_names() const {
        std::vector<std::string> names;
        names.reserve(column_count);
        std::apply([&names](const auto&... cols) {
            (names.emplace_back(cols.name), ...);
        }, columns);
        return names;
    }
};

// Factory
template <typename Entity, typename... Columns>
constexpr auto table(const char* name, Columns... cols) {
    return TableDef<Entity, Columns...>{name, std::make_tuple(cols...)};
}

} // namespace cpplinq
