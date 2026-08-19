#pragma once
#include <string>
#include <string_view>
#include <cstddef>
#include <optional>
#include "cpplinq/mapping/type_traits.h"

namespace cpplinq {

class ISqlDialect {
public:
    virtual ~ISqlDialect() = default;

    // Identifier quoting: "table" vs [table] vs `table`
    virtual std::string quote_id(std::string_view id) const = 0;

    // Parameter placeholder: ? vs $1 vs @p1
    virtual std::string placeholder(size_t index) const = 0;

    // LIMIT/OFFSET syntax
    virtual std::string limit_offset(std::optional<size_t> limit,
                                     std::optional<size_t> offset) const = 0;

    // SELECT prefix LIMIT/OFFSET (e.g. SKIP offset FIRST limit in Informix)
    virtual std::string select_prefix_limit_offset(std::optional<size_t> limit,
                                                  std::optional<size_t> offset) const {
        (void)limit;
        (void)offset;
        return "";
    }

    // SQL type name for a given SqlType
    virtual std::string type_name(SqlType type) const = 0;

    // Auto-increment column definition
    virtual std::string auto_increment_type() const = 0;

    // RETURNING clause for INSERT (PostgreSQL, SQLite)
    virtual std::string returning_clause(std::string_view column) const = 0;

    // OUTPUT clause for INSERT (T-SQL / MSSQL - inserted before VALUES)
    virtual std::string output_clause(std::string_view column) const {
        (void)column;
        return "";
    }

    // Query to retrieve last inserted auto-increment ID
    virtual std::string last_insert_id_query() const {
        return "SELECT LAST_INSERT_ID()";
    }

    // CREATE TABLE prefix (SQLite/PG vs MSSQL IF OBJECT_ID)
    virtual std::string create_table_prefix(std::string_view table_name) const {
        return "CREATE TABLE IF NOT EXISTS " + quote_id(table_name);
    }

    // Function name mapping (e.g. LENGTH -> LEN in MSSQL)
    virtual std::string function_name(std::string_view func) const {
        return std::string(func);
    }

    // Date/time functions
    virtual std::string current_timestamp_func() const { return "CURRENT_TIMESTAMP"; }
    virtual std::string current_date_func() const { return "CURRENT_DATE"; }
    virtual std::string extract_part_func(std::string_view part, std::string_view expr_sql) const {
        return "EXTRACT(" + std::string(part) + " FROM CAST(" + std::string(expr_sql) + " AS TIMESTAMP))";
    }
    virtual std::string date_add_days_func(std::string_view expr_sql, std::string_view days_sql) const {
        return "(CAST(" + std::string(expr_sql) + " AS TIMESTAMP) + (" + std::string(days_sql) + " || ' days')::interval)";
    }

    // UPSERT statement generation
    virtual std::string generate_upsert(
        std::string_view table_name,
        const std::vector<std::string>& insert_columns,
        const std::vector<BoundValue>& values,
        const std::vector<std::string>& conflict_columns,
        const std::vector<std::string>& update_columns
    ) const {
        (void)values;
        return generate_upsert(table_name, insert_columns, conflict_columns, update_columns);
    }

    virtual std::string generate_upsert(
        std::string_view table_name,
        const std::vector<std::string>& insert_columns,
        const std::vector<std::string>& conflict_columns,
        const std::vector<std::string>& update_columns
    ) const {
        std::string sql = "INSERT INTO " + quote_id(table_name) + " (";
        for (size_t i = 0; i < insert_columns.size(); ++i) {
            if (i > 0) sql += ", ";
            sql += quote_id(insert_columns[i]);
        }
        sql += ") VALUES (";
        for (size_t i = 0; i < insert_columns.size(); ++i) {
            if (i > 0) sql += ", ";
            sql += placeholder(i);
        }
        sql += ") ON CONFLICT (";
        for (size_t i = 0; i < conflict_columns.size(); ++i) {
            if (i > 0) sql += ", ";
            sql += quote_id(conflict_columns[i]);
        }
        sql += ") DO UPDATE SET ";
        for (size_t i = 0; i < update_columns.size(); ++i) {
            if (i > 0) sql += ", ";
            sql += quote_id(update_columns[i]) + " = EXCLUDED." + quote_id(update_columns[i]);
        }
        return sql;
    }
};

} // namespace cpplinq
