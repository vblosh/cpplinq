#pragma once
#include "cpplinq/driver/connection.h"
#include "cpplinq/core/query_builder.h"
#include "cpplinq/core/table.h"
#include "cpplinq/mapping/row_mapper.h"
#if __has_include("cpplinq/core/sql_generator.h")
#include "cpplinq/core/sql_generator.h"
#else
#ifndef CPPLINQ_COLUMN_INFO_DEFINED
#define CPPLINQ_COLUMN_INFO_DEFINED
namespace cpplinq {
struct ColumnInfo {
    std::string name;
    SqlType sql_type = SqlType::Integer;
    bool is_primary_key = false;
    bool is_auto_increment = false;
    bool is_not_null = false;
    bool is_nullable = false;
    bool is_unique = false;
};
} // namespace cpplinq
#endif
#endif
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <tuple>
#include <optional>
#include <utility>

namespace cpplinq {

template <typename Backend>
class DbContext {
public:
    explicit DbContext(const std::string& connection_string)
        : conn_storage_(make_connection<Backend>(connection_string)),
          conn_(conn_storage_.get()) {
        conn_->open();
    }

    explicit DbContext(IConnection* conn)
        : conn_storage_(nullptr),
          conn_(conn) {}

    explicit DbContext(IConnection& conn)
        : conn_storage_(nullptr),
          conn_(&conn) {}

    ~DbContext() {
        if (conn_storage_ && conn_storage_->is_open()) {
            try { conn_storage_->close(); } catch(...) {}
        }
    }

    // Non-copyable, movable
    DbContext(const DbContext&) = delete;
    DbContext& operator=(const DbContext&) = delete;
    DbContext(DbContext&&) = default;
    DbContext& operator=(DbContext&&) = default;

    // Ensure table exists (CREATE TABLE IF NOT EXISTS)
    template <typename Entity, typename... Cols>
    void ensure_table(const TableDef<Entity, Cols...>& table) {
        SqlGenerator gen(conn_->dialect());
        std::vector<ColumnInfo> col_infos;
        col_infos.reserve(sizeof...(Cols));
        std::apply([&col_infos](const auto&... cols) {
            (col_infos.push_back(make_column_info(cols)), ...);
        }, table.columns);
        auto result = gen.generate_create_table(std::string(table.name), col_infos);
        conn_->execute(result.sql);
    }

    // Start a deferred query
    template <typename Entity, typename... Cols>
    QueryBuilder<Entity, Cols...> from(const TableDef<Entity, Cols...>& table) {
        return QueryBuilder<Entity, Cols...>(*conn_, std::string(table.name), table.columns);
    }

    // Insert a single entity, returns the last inserted row id
    template <typename Entity, typename... Cols>
    int64_t insert(const TableDef<Entity, Cols...>& table, const Entity& entity) {
        SqlGenerator gen(conn_->dialect());
        std::vector<std::string> col_names;
        std::vector<BoundValue> values;
        std::string pk_col;

        std::apply([&](const auto&... cols) {
            auto process = [&](const auto& col) {
                if (col.is_auto_increment) {
                    pk_col = std::string(col.name);
                    return; // Skip auto-increment columns on insert
                }
                col_names.emplace_back(col.name);
                values.push_back(field_to_bound_value(entity, col.member_ptr));
            };
            (process(cols), ...);
        }, table.columns);

        std::optional<std::string_view> returning;
        if (!pk_col.empty()) returning = pk_col;

        auto result = gen.generate_insert(std::string(table.name), col_names, values, returning);
        auto stmt = conn_->prepare(result.sql);
        for (size_t i = 0; i < result.params.size(); ++i) {
            stmt->bind(static_cast<int>(i), result.params[i]);
        }

        if (!pk_col.empty()) {
            auto reader = stmt->execute_query();
            if (reader && reader->next()) {
                return reader->get_int64(0);
            }
            return 0;
        }
        stmt->execute_non_query();
        return 0;
    }

    // Insert multiple entities in a transaction
    template <typename Entity, typename... Cols>
    void insert_many(const TableDef<Entity, Cols...>& table,
                     const std::vector<Entity>& entities) {
        Transaction txn(*conn_);
        for (const auto& entity : entities) {
            insert(table, entity);
        }
        txn.commit();
    }

    // Upsert (insert or update on conflict)
    template <typename Entity, typename... Cols>
    size_t upsert(
        const TableDef<Entity, Cols...>& table,
        const Entity& entity,
        const std::vector<std::string>& conflict_columns = {},
        const std::vector<std::string>& update_columns = {}
    ) {
        SqlGenerator gen(conn_->dialect());
        std::vector<std::string> insert_cols;
        std::vector<BoundValue> values;
        std::vector<std::string> auto_conflict_cols = conflict_columns;

        std::apply([&](const auto&... cols) {
            auto process = [&](const auto& col) {
                insert_cols.emplace_back(col.name);
                values.push_back(field_to_bound_value(entity, col.member_ptr));
                if (col.is_primary_key && auto_conflict_cols.empty()) {
                    auto_conflict_cols.emplace_back(col.name);
                }
            };
            (process(cols), ...);
        }, table.columns);

        std::vector<std::string> actual_update_cols = update_columns;
        if (actual_update_cols.empty()) {
            for (const auto& col_name : insert_cols) {
                bool is_conflict = false;
                for (const auto& c : auto_conflict_cols) {
                    if (c == col_name) {
                        is_conflict = true;
                        break;
                    }
                }
                if (!is_conflict) {
                    actual_update_cols.push_back(col_name);
                }
            }
        }

        auto result = gen.generate_upsert(
            std::string(table.name),
            insert_cols,
            values,
            auto_conflict_cols,
            actual_update_cols
        );

        auto stmt = conn_->prepare(result.sql);
        for (size_t i = 0; i < result.params.size(); ++i) {
            stmt->bind(static_cast<int>(i), result.params[i]);
        }
        return stmt->execute_non_query();
    }

    // Direct SQL execution
    void execute_raw(std::string_view sql) {
        conn_->execute(sql);
    }

    // Transaction scope
    Transaction begin_transaction() {
        return Transaction(*conn_);
    }

    IConnection& connection() { return *conn_; }

private:
    std::unique_ptr<IConnection> conn_storage_;
    IConnection* conn_ = nullptr;

    template <typename Col>
    static ColumnInfo make_column_info(const Col& col) {
        using FieldType = typename Col::value_type;
        ColumnInfo info;
        info.name = std::string(col.name);
        if constexpr (is_nullable_v<FieldType>) {
            using InnerType = typename FieldType::value_type;
            info.sql_type = sql_type_of<InnerType>::value;
            info.is_nullable = true;
        } else {
            info.sql_type = sql_type_of<FieldType>::value;
            info.is_nullable = false;
        }
        info.is_primary_key = col.is_primary_key;
        info.is_auto_increment = col.is_auto_increment;
        info.is_not_null = col.is_not_null;
        info.is_unique = col.is_unique;
        return info;
    }
};

// Convenience factory function
template <typename Backend>
DbContext<Backend> connect(const std::string& connection_string) {
    return DbContext<Backend>(connection_string);
}

} // namespace cpplinq
