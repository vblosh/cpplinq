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

    // CREATE TABLE prefix (SQLite/PG vs MSSQL IF OBJECT_ID)
    virtual std::string create_table_prefix(std::string_view table_name) const {
        return "CREATE TABLE IF NOT EXISTS " + quote_id(table_name);
    }

    // Function name mapping (e.g. LENGTH -> LEN in MSSQL)
    virtual std::string function_name(std::string_view func) const {
        return std::string(func);
    }
};

} // namespace cpplinq
