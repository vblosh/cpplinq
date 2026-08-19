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
            std::string ret_clause = conn_->dialect().returning_clause(pk_col);
            std::string out_clause = conn_->dialect().output_clause(pk_col);
            if (!ret_clause.empty() || !out_clause.empty()) {
                auto reader = stmt->execute_query();
                if (reader && reader->next()) {
                    return reader->get_int64(0);
                }
                return 0;
            }
            // Dialects without RETURNING/OUTPUT (e.g. MySQL)
            stmt->execute_non_query();
            try {
                auto last_id_stmt = conn_->prepare("SELECT LAST_INSERT_ID()");
                auto r = last_id_stmt->execute_query();
                if (r && r->next()) {
                    return r->get_int64(0);
                }
            } catch (...) {}
            return 0;
        }
        stmt->execute_non_query();
        return 0;
    }

    // Insert multiple entities with batch chunking
    template <typename Entity, typename... Cols>
    void insert_many(const TableDef<Entity, Cols...>& table,
                     const std::vector<Entity>& entities,
                     size_t chunk_size = 0) {
        if (entities.empty()) return;
        if (chunk_size == 0) {
            chunk_size = conn_->capabilities().default_batch_chunk_size;
            if (chunk_size == 0) chunk_size = 1000;
        }

        if (conn_->capabilities().array_batch_insert) {
            SqlGenerator gen(conn_->dialect());
            std::vector<std::string> col_names;
            std::apply([&](const auto&... cols) {
                auto add = [&](const auto& col) {
                    if (!col.is_auto_increment) col_names.emplace_back(col.name);
                };
                (add(cols), ...);
            }, table.columns);

            std::vector<BoundValue> dummy(col_names.size(), BoundValue{int64_t{0}});
            auto result = gen.generate_insert(std::string(table.name), col_names, dummy, std::nullopt);
            const size_t ncols = col_names.size();

            Transaction txn(*conn_);
            for (size_t offset = 0; offset < entities.size(); offset += chunk_size) {
                size_t count = std::min(chunk_size, entities.size() - offset);
                std::vector<BoundValue> flat;
                flat.reserve(count * ncols);

                for (size_t r = 0; r < count; ++r) {
                    const auto& e = entities[offset + r];
                    std::apply([&](const auto&... cols) {
                        auto push = [&](const auto& col) {
                            if (!col.is_auto_increment) {
                                flat.push_back(field_to_bound_value(e, col.member_ptr));
                            }
                        };
                        (push(cols), ...);
                    }, table.columns);
                }
                conn_->insert_many_batch(result.sql, flat, ncols, count);
            }
            txn.commit();
            return;
        }

        // Fallback: row-by-row prepared statement reuse loop
        SqlGenerator gen(conn_->dialect());
        std::vector<std::string> col_names;
        std::apply([&](const auto&... cols) {
            auto add = [&](const auto& col) {
                if (!col.is_auto_increment) col_names.emplace_back(col.name);
            };
            (add(cols), ...);
        }, table.columns);

        std::vector<BoundValue> dummy(col_names.size(), BoundValue{int64_t{0}});
        auto result = gen.generate_insert(std::string(table.name), col_names, dummy, std::nullopt);
        auto stmt = conn_->prepare(result.sql);

        Transaction txn(*conn_);
        for (const auto& entity : entities) {
            stmt->reset();
            int idx = 0;
            std::apply([&](const auto&... cols) {
                auto bind = [&](const auto& col) {
                    if (!col.is_auto_increment) {
                        stmt->bind(idx++, field_to_bound_value(entity, col.member_ptr));
                    }
                };
                (bind(cols), ...);
            }, table.columns);
            stmt->execute_non_query();
        }
        txn.commit();
    }

    // Delete multiple entities by primary key with chunking and ODBC array binding fast path
    template <typename Entity, typename PkType, typename... Cols>
    size_t delete_many(const TableDef<Entity, Cols...>& table,
                       const std::vector<PkType>& ids,
                       size_t chunk_size = 0) {
        if (ids.empty()) return 0;
        if (chunk_size == 0) {
            chunk_size = conn_->capabilities().default_batch_chunk_size;
            if (chunk_size == 0) chunk_size = 1000;
        }

        std::string pk_col;
        std::apply([&](const auto&... cols) {
            auto find_pk = [&](const auto& col) {
                if (col.is_primary_key && pk_col.empty()) pk_col = std::string(col.name);
            };
            (find_pk(cols), ...);
        }, table.columns);

        if (pk_col.empty()) {
            throw DbException("delete_many requires a primary key column");
        }

        const ISqlDialect& d = conn_->dialect();

        // ODBC Array Binding Fast Path: DELETE FROM table WHERE pk = ?
        if (conn_->capabilities().array_batch_insert) {
            std::string sql = "DELETE FROM " + d.quote_id(table.name) + " WHERE " + d.quote_id(pk_col) + " = " + d.placeholder(0);
            size_t total_deleted = 0;

            Transaction txn(*conn_);
            for (size_t offset = 0; offset < ids.size(); offset += chunk_size) {
                size_t count = std::min(chunk_size, ids.size() - offset);
                std::vector<BoundValue> flat;
                flat.reserve(count);

                for (size_t i = 0; i < count; ++i) {
                    const auto& val = ids[offset + i];
                    if constexpr (std::is_integral_v<PkType>) {
                        flat.emplace_back(static_cast<int64_t>(val));
                    } else if constexpr (std::is_floating_point_v<PkType>) {
                        flat.emplace_back(static_cast<double>(val));
                    } else if constexpr (std::is_same_v<PkType, std::string> || std::is_same_v<PkType, std::string_view>) {
                        flat.emplace_back(std::string(val));
                    } else {
                        flat.emplace_back(val);
                    }
                }
                total_deleted += conn_->insert_many_batch(sql, flat, 1, count);
            }
            txn.commit();
            return total_deleted;
        }

        // Fallback: chunked IN (?, ?, ...) statements
        size_t total_deleted = 0;
        Transaction txn(*conn_);
        for (size_t offset = 0; offset < ids.size(); offset += chunk_size) {
            size_t count = std::min(chunk_size, ids.size() - offset);
            std::string sql = "DELETE FROM " + d.quote_id(table.name) + " WHERE " + d.quote_id(pk_col) + " IN (";
            for (size_t i = 0; i < count; ++i) {
                if (i > 0) sql += ", ";
                sql += d.placeholder(i);
            }
            sql += ")";

            auto stmt = conn_->prepare(sql);
            for (size_t i = 0; i < count; ++i) {
                const auto& val = ids[offset + i];
                if constexpr (std::is_integral_v<PkType>) {
                    stmt->bind(static_cast<int>(i), BoundValue{static_cast<int64_t>(val)});
                } else if constexpr (std::is_floating_point_v<PkType>) {
                    stmt->bind(static_cast<int>(i), BoundValue{static_cast<double>(val)});
                } else if constexpr (std::is_same_v<PkType, std::string> || std::is_same_v<PkType, std::string_view>) {
                    stmt->bind(static_cast<int>(i), BoundValue{std::string(val)});
                } else {
                    stmt->bind(static_cast<int>(i), BoundValue{val});
                }
            }
            total_deleted += stmt->execute_non_query();
        }
        txn.commit();
        return total_deleted;
    }

    // Update multiple entities by primary key with chunking and ODBC fast path
    template <typename Entity, typename... Cols>
    size_t update_many(const TableDef<Entity, Cols...>& table,
                       const std::vector<Entity>& entities,
                       size_t chunk_size = 0) {
        if (entities.empty()) return 0;
        if (chunk_size == 0) {
            chunk_size = conn_->capabilities().default_batch_chunk_size;
            if (chunk_size == 0) chunk_size = 1000;
        }

        std::string pk_col;
        std::vector<std::string> update_cols;
        std::apply([&](const auto&... cols) {
            auto scan = [&](const auto& col) {
                if (col.is_primary_key) pk_col = std::string(col.name);
                else update_cols.emplace_back(col.name);
            };
            (scan(cols), ...);
        }, table.columns);

        if (pk_col.empty()) {
            throw DbException("update_many requires a primary key column");
        }

        const ISqlDialect& d = conn_->dialect();
        std::string sql = "UPDATE " + d.quote_id(table.name) + " SET ";
        for (size_t i = 0; i < update_cols.size(); ++i) {
            if (i > 0) sql += ", ";
            sql += d.quote_id(update_cols[i]) + " = " + d.placeholder(i);
        }
        sql += " WHERE " + d.quote_id(pk_col) + " = " + d.placeholder(update_cols.size());

        if (conn_->capabilities().array_batch_insert) {
            const size_t ncols = update_cols.size() + 1;
            size_t total_affected = 0;

            Transaction txn(*conn_);
            for (size_t offset = 0; offset < entities.size(); offset += chunk_size) {
                size_t count = std::min(chunk_size, entities.size() - offset);
                std::vector<BoundValue> flat;
                flat.reserve(count * ncols);

                for (size_t r = 0; r < count; ++r) {
                    const auto& e = entities[offset + r];
                    BoundValue pk_val;
                    std::apply([&](const auto&... cols) {
                        auto push = [&](const auto& col) {
                            if (!col.is_primary_key) {
                                flat.push_back(field_to_bound_value(e, col.member_ptr));
                            } else {
                                pk_val = field_to_bound_value(e, col.member_ptr);
                            }
                        };
                        (push(cols), ...);
                    }, table.columns);
                    flat.push_back(pk_val);
                }
                total_affected += conn_->insert_many_batch(sql, flat, ncols, count);
            }
            txn.commit();
            return total_affected;
        }

        // Fallback: row-by-row transaction loop with prepared statement reuse
        size_t total = 0;
        auto stmt = conn_->prepare(sql);
        Transaction txn(*conn_);
        for (const auto& e : entities) {
            stmt->reset();
            int idx = 0;
            BoundValue pk_val;
            std::apply([&](const auto&... cols) {
                auto bind = [&](const auto& col) {
                    if (!col.is_primary_key) {
                        stmt->bind(idx++, field_to_bound_value(e, col.member_ptr));
                    } else {
                        pk_val = field_to_bound_value(e, col.member_ptr);
                    }
                };
                (bind(cols), ...);
            }, table.columns);
            stmt->bind(idx, pk_val);
            total += stmt->execute_non_query();
        }
        txn.commit();
        return total;
    }

    // Upsert multiple entities in a transaction with prepared statement reuse
    template <typename Entity, typename... Cols>
    size_t upsert_many(const TableDef<Entity, Cols...>& table,
                       const std::vector<Entity>& entities,
                       const std::vector<std::string>& conflict_columns = {},
                       const std::vector<std::string>& update_columns = {}) {
        if (entities.empty()) return 0;

        SqlGenerator gen(conn_->dialect());
        std::vector<std::string> insert_cols;
        std::vector<std::string> auto_conflict_cols = conflict_columns;

        std::apply([&](const auto&... cols) {
            auto process = [&](const auto& col) {
                insert_cols.emplace_back(col.name);
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

        std::vector<BoundValue> dummy_values(insert_cols.size(), BoundValue{int64_t{0}});
        auto result = gen.generate_upsert(
            std::string(table.name),
            insert_cols,
            dummy_values,
            auto_conflict_cols,
            actual_update_cols
        );

        auto stmt = conn_->prepare(result.sql);
        size_t total = 0;
        Transaction txn(*conn_);
        for (const auto& e : entities) {
            stmt->reset();
            int idx = 0;
            std::apply([&](const auto&... cols) {
                auto bind = [&](const auto& col) {
                    stmt->bind(idx++, field_to_bound_value(e, col.member_ptr));
                };
                (bind(cols), ...);
            }, table.columns);
            total += stmt->execute_non_query();
        }
        txn.commit();
        return total;
    }

    // Truncate table (TRUNCATE TABLE with DELETE FROM fallback)
    template <typename Entity, typename... Cols>
    void truncate(const TableDef<Entity, Cols...>& table) {
        const ISqlDialect& d = conn_->dialect();
        try {
            conn_->execute("TRUNCATE TABLE " + d.quote_id(table.name));
        } catch (...) {
            conn_->execute("DELETE FROM " + d.quote_id(table.name));
        }
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
