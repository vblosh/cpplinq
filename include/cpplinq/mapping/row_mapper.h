#pragma once
#include "cpplinq/driver/connection.h"
#include "cpplinq/mapping/type_traits.h"
#include "cpplinq/mapping/data_types.h"
#include <tuple>
#include <string>
#include <optional>
#include <cstdint>
#include <vector>
#include <type_traits>
#include <utility>
#include <chrono>

namespace cpplinq {

template <typename TargetType>
TargetType read_column_value(IDataReader& reader, int col_idx) {
    using U = std::remove_cvref_t<TargetType>;
    if constexpr (std::is_same_v<U, bool>) {
        return reader.get_bool(col_idx);
    } else if constexpr (std::is_integral_v<U>) {
        if constexpr (std::is_unsigned_v<U>) {
            return static_cast<U>(reader.get_uint64(col_idx));
        } else {
            return static_cast<U>(reader.get_int64(col_idx));
        }
    } else if constexpr (std::is_floating_point_v<U>) {
        return static_cast<U>(reader.get_double(col_idx));
    } else if constexpr (std::is_same_v<U, std::string>) {
        return reader.get_string(col_idx);
    } else if constexpr (std::is_same_v<U, std::vector<uint8_t>>) {
        return reader.get_blob(col_idx);
    } else if constexpr (std::is_same_v<U, SqlNumeric>) {
        return reader.get_numeric(col_idx);
    } else if constexpr (std::is_same_v<U, SqlDate>) {
        return reader.get_date(col_idx);
    } else if constexpr (std::is_same_v<U, SqlTime>) {
        return reader.get_time(col_idx);
    } else if constexpr (std::is_same_v<U, SqlTimestamp>) {
        return reader.get_timestamp(col_idx);
    } else if constexpr (std::is_same_v<U, std::chrono::system_clock::time_point>) {
        return reader.get_timestamp(col_idx).to_time_point();
    } else if constexpr (std::is_same_v<U, SqlInterval>) {
        return reader.get_interval(col_idx);
    } else if constexpr (requires(SqlInterval iv) { iv.template to_duration<U>(); }) {
        return reader.get_interval(col_idx).template to_duration<U>();
    } else {
        return U{};
    }
}

template <typename T>
BoundValue convert_to_bound_value(const T& val) {
    using U = std::remove_cvref_t<T>;
    if constexpr (std::is_same_v<U, bool>) {
        return BoundValue{val};
    } else if constexpr (std::is_integral_v<U>) {
        if constexpr (std::is_unsigned_v<U>) {
            return BoundValue{static_cast<uint64_t>(val)};
        } else {
            return BoundValue{static_cast<int64_t>(val)};
        }
    } else if constexpr (std::is_floating_point_v<U>) {
        return BoundValue{static_cast<double>(val)};
    } else if constexpr (std::is_same_v<U, std::string>) {
        return BoundValue{val};
    } else if constexpr (std::is_same_v<U, std::string_view>) {
        return BoundValue{std::string(val)};
    } else if constexpr (std::is_same_v<U, const char*>) {
        return BoundValue{std::string(val ? val : "")};
    } else if constexpr (std::is_same_v<U, std::vector<uint8_t>>) {
        return BoundValue{val};
    } else if constexpr (std::is_same_v<U, SqlNumeric>) {
        return BoundValue{val};
    } else if constexpr (std::is_same_v<U, SqlDate>) {
        return BoundValue{val};
    } else if constexpr (std::is_same_v<U, SqlTime>) {
        return BoundValue{val};
    } else if constexpr (std::is_same_v<U, SqlTimestamp>) {
        return BoundValue{val};
    } else if constexpr (std::is_same_v<U, std::chrono::system_clock::time_point>) {
        return BoundValue{SqlTimestamp::from_time_point(val)};
    } else if constexpr (std::is_same_v<U, SqlInterval>) {
        return BoundValue{val};
    } else if constexpr (requires { SqlInterval::from_duration(val); }) {
        return BoundValue{SqlInterval::from_duration(val)};
    } else {
        return BoundValue{std::monostate{}};
    }
}

template <typename Entity, typename... ColumnDefs>
class RowMapper {
public:
    explicit RowMapper(const std::tuple<ColumnDefs...>& columns, int offset = 0)
        : columns_(columns), offset_(offset) {}

    Entity map_row(IDataReader& reader) const {
        Entity entity{};
        map_columns(entity, reader, std::index_sequence_for<ColumnDefs...>{});
        return entity;
    }

    void map_row(IDataReader& reader, Entity& entity) const {
        map_columns(entity, reader, std::index_sequence_for<ColumnDefs...>{});
    }

    bool is_all_null(IDataReader& reader) const {
        return check_all_null(reader, std::index_sequence_for<ColumnDefs...>{});
    }

private:
    const std::tuple<ColumnDefs...>& columns_;
    int offset_ = 0;

    template <size_t... Is>
    bool check_all_null(IDataReader& reader, std::index_sequence<Is...>) const {
        return (reader.is_null(static_cast<int>(Is) + offset_) && ...);
    }

    template <size_t... Is>
    void map_columns(Entity& entity, IDataReader& reader, std::index_sequence<Is...>) const {
        (map_single_column<Is>(entity, reader), ...);
    }

    template <size_t I>
    void map_single_column(Entity& entity, IDataReader& reader) const {
        const auto& col = std::get<I>(columns_);
        using FieldType = std::remove_cvref_t<decltype(entity.*(col.member_ptr))>;
        int col_idx = static_cast<int>(I) + offset_;

        if (reader.is_null(col_idx)) {
            if constexpr (is_nullable_v<FieldType>) {
                entity.*(col.member_ptr) = std::nullopt;
            }
            return;
        }

        if constexpr (is_nullable_v<FieldType>) {
            // Handle optional types
            using InnerType = typename FieldType::value_type;
            entity.*(col.member_ptr) = read_column_value<InnerType>(reader, col_idx);
        } else {
            // Handle non-optional types
            entity.*(col.member_ptr) = read_column_value<FieldType>(reader, col_idx);
        }
    }
};

// Helper to extract BoundValue from an entity field for INSERT/UPDATE
template <typename Entity, typename FieldType>
BoundValue field_to_bound_value(const Entity& entity, FieldType Entity::* member_ptr) {
    using T = std::remove_cvref_t<FieldType>;
    const auto& val = entity.*member_ptr;
    
    if constexpr (is_nullable_v<T>) {
        if (!val.has_value()) return BoundValue{std::monostate{}};
        return convert_to_bound_value(*val);
    } else {
        return convert_to_bound_value(val);
    }
}

} // namespace cpplinq
