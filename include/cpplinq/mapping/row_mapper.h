#pragma once
#include "cpplinq/driver/connection.h"
#include "cpplinq/mapping/type_traits.h"
#include <tuple>
#include <string>
#include <optional>
#include <cstdint>
#include <vector>
#include <type_traits>
#include <utility>

namespace cpplinq {

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
            if constexpr (std::is_same_v<InnerType, int> || std::is_same_v<InnerType, int64_t>) {
                entity.*(col.member_ptr) = static_cast<InnerType>(reader.get_int64(col_idx));
            } else if constexpr (std::is_same_v<InnerType, double> || std::is_same_v<InnerType, float>) {
                entity.*(col.member_ptr) = static_cast<InnerType>(reader.get_double(col_idx));
            } else if constexpr (std::is_same_v<InnerType, std::string>) {
                entity.*(col.member_ptr) = reader.get_string(col_idx);
            } else if constexpr (std::is_same_v<InnerType, bool>) {
                entity.*(col.member_ptr) = reader.get_bool(col_idx);
            } else if constexpr (std::is_same_v<InnerType, std::vector<uint8_t>>) {
                entity.*(col.member_ptr) = reader.get_blob(col_idx);
            }
        } else {
            // Handle non-optional types
            if constexpr (std::is_same_v<FieldType, int>) {
                entity.*(col.member_ptr) = static_cast<int>(reader.get_int64(col_idx));
            } else if constexpr (std::is_same_v<FieldType, int64_t>) {
                entity.*(col.member_ptr) = reader.get_int64(col_idx);
            } else if constexpr (std::is_same_v<FieldType, double>) {
                entity.*(col.member_ptr) = reader.get_double(col_idx);
            } else if constexpr (std::is_same_v<FieldType, float>) {
                entity.*(col.member_ptr) = static_cast<float>(reader.get_double(col_idx));
            } else if constexpr (std::is_same_v<FieldType, std::string>) {
                entity.*(col.member_ptr) = reader.get_string(col_idx);
            } else if constexpr (std::is_same_v<FieldType, bool>) {
                entity.*(col.member_ptr) = reader.get_bool(col_idx);
            } else if constexpr (std::is_same_v<FieldType, std::vector<uint8_t>>) {
                entity.*(col.member_ptr) = reader.get_blob(col_idx);
            }
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
        using InnerType = typename T::value_type;
        if constexpr (std::is_same_v<InnerType, int>)
            return BoundValue{static_cast<int64_t>(*val)};
        else if constexpr (std::is_same_v<InnerType, int64_t>)
            return BoundValue{*val};
        else if constexpr (std::is_same_v<InnerType, double> || std::is_same_v<InnerType, float>)
            return BoundValue{static_cast<double>(*val)};
        else if constexpr (std::is_same_v<InnerType, std::string>)
            return BoundValue{*val};
        else if constexpr (std::is_same_v<InnerType, bool>)
            return BoundValue{*val};
        else if constexpr (std::is_same_v<InnerType, std::vector<uint8_t>>)
            return BoundValue{*val};
        else
            return BoundValue{std::monostate{}};
    } else {
        if constexpr (std::is_same_v<T, int>)
            return BoundValue{static_cast<int64_t>(val)};
        else if constexpr (std::is_same_v<T, int64_t>)
            return BoundValue{val};
        else if constexpr (std::is_same_v<T, double> || std::is_same_v<T, float>)
            return BoundValue{static_cast<double>(val)};
        else if constexpr (std::is_same_v<T, std::string>)
            return BoundValue{val};
        else if constexpr (std::is_same_v<T, bool>)
            return BoundValue{val};
        else if constexpr (std::is_same_v<T, std::vector<uint8_t>>)
            return BoundValue{val};
        else
            return BoundValue{std::monostate{}};
    }
}

} // namespace cpplinq
