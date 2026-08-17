#pragma once

#include "cpplinq/core/expression.h"
#include "cpplinq/dialect/dialect.h"
#include "cpplinq/driver/connection.h"
#include "cpplinq/mapping/type_traits.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cpplinq {

// Generated SQL query along with bound parameters
struct GeneratedSql {
    std::string sql;
    std::vector<BoundValue> params;

    bool operator==(const GeneratedSql& other) const = default;
};

// Join clause metadata
struct JoinClause {
    std::string join_type; // "INNER JOIN", "LEFT JOIN", etc.
    std::string table_name;
    expr::ExprNode on_condition;
};

// Set operation types
enum class SetOpType {
    Union,
    UnionAll,
    Intersect,
    Except
};

// CTE clause metadata
struct CteClause {
    std::string name;
    expr::SubqueryExpr subquery;
    bool is_recursive = false;
};

// Set operation clause metadata
struct SetOpClause {
    SetOpType op_type;
    std::string table_name;
    std::vector<std::string> columns;
    std::optional<expr::ExprNode> where;
    bool is_distinct = false;
};

// Column metadata for schema generation (e.g. CREATE TABLE)
struct ColumnInfo {
    std::string name;
    SqlType sql_type = SqlType::Text;
    bool is_primary_key = false;
    bool is_auto_increment = false;
    bool is_not_null = false;
    bool is_nullable = false;
    bool is_unique = false;
};

class SqlGenerator {
public:
    explicit SqlGenerator(const ISqlDialect& dialect);

    // Common Table Expression (CTE) queries (WITH ... SELECT ...)
    GeneratedSql generate_cte_select(
        const std::vector<CteClause>& ctes,
        std::string_view table_name,
        const std::vector<std::string>& columns = {},
        const std::optional<expr::ExprNode>& where = std::nullopt,
        const std::vector<std::pair<expr::ExprNode, expr::SortDir>>& order_by = {},
        std::optional<size_t> limit = std::nullopt,
        std::optional<size_t> offset = std::nullopt,
        bool is_distinct = false
    ) const;

    // SELECT queries
    GeneratedSql generate_select(
        std::string_view table_name,
        const std::vector<std::string>& columns = {},
        const std::optional<expr::ExprNode>& where = std::nullopt,
        const std::vector<std::pair<expr::ExprNode, expr::SortDir>>& order_by = {},
        std::optional<size_t> limit = std::nullopt,
        std::optional<size_t> offset = std::nullopt,
        bool is_distinct = false,
        const std::vector<expr::ExprNode>& group_by = {},
        const std::optional<expr::ExprNode>& having = std::nullopt
    ) const;

    GeneratedSql generate_select(
        std::string_view table_name,
        const std::vector<std::string>& columns,
        const std::optional<expr::ExprNode>& where,
        const std::vector<expr::OrderByExpr>& order_by,
        std::optional<size_t> limit = std::nullopt,
        std::optional<size_t> offset = std::nullopt,
        bool is_distinct = false,
        const std::vector<expr::ExprNode>& group_by = {},
        const std::optional<expr::ExprNode>& having = std::nullopt
    ) const;

    // Joined SELECT queries
    GeneratedSql generate_joined_select(
        std::string_view primary_table,
        const std::vector<std::string>& primary_columns,
        const std::vector<JoinClause>& joins,
        const std::vector<std::pair<std::string, std::vector<std::string>>>& joined_tables_columns,
        const std::optional<expr::ExprNode>& where = std::nullopt,
        const std::vector<std::pair<expr::ExprNode, expr::SortDir>>& order_by = {},
        std::optional<size_t> limit = std::nullopt,
        std::optional<size_t> offset = std::nullopt,
        bool is_distinct = false
    ) const;

    // Set operations (UNION, UNION ALL, INTERSECT, EXCEPT)
    GeneratedSql generate_set_operation(
        std::string_view base_table,
        const std::vector<std::string>& base_columns,
        const std::optional<expr::ExprNode>& base_where,
        bool base_distinct,
        const std::vector<SetOpClause>& operations,
        const std::vector<std::pair<expr::ExprNode, expr::SortDir>>& order_by = {},
        std::optional<size_t> limit = std::nullopt,
        std::optional<size_t> offset = std::nullopt
    ) const;

    // INSERT queries
    GeneratedSql generate_insert(
        std::string_view table_name,
        const std::vector<std::string>& column_names,
        const std::vector<BoundValue>& values,
        std::optional<std::string_view> returning_column = std::nullopt
    ) const;

    // UPSERT queries
    GeneratedSql generate_upsert(
        std::string_view table_name,
        const std::vector<std::string>& insert_columns,
        const std::vector<BoundValue>& values,
        const std::vector<std::string>& conflict_columns,
        const std::vector<std::string>& update_columns
    ) const;

    // UPDATE queries
    GeneratedSql generate_update(
        std::string_view table_name,
        const std::vector<expr::AssignExpr>& assignments,
        const std::optional<expr::ExprNode>& where = std::nullopt
    ) const;

    GeneratedSql generate_update(
        std::string_view table_name,
        const std::vector<std::pair<std::string, BoundValue>>& assignments,
        const std::optional<expr::ExprNode>& where = std::nullopt
    ) const;

    // DELETE queries
    GeneratedSql generate_delete(
        std::string_view table_name,
        const std::optional<expr::ExprNode>& where = std::nullopt
    ) const;

    // DDL queries
    GeneratedSql generate_create_table(
        std::string_view table_name,
        const std::vector<ColumnInfo>& column_infos
    ) const;

    // Aggregate queries
    GeneratedSql generate_count(
        std::string_view table_name,
        const std::optional<expr::ExprNode>& where = std::nullopt,
        bool is_distinct = false,
        std::optional<std::string_view> distinct_column = std::nullopt
    ) const;

    GeneratedSql generate_aggregate(
        std::string_view function_name,
        std::string_view table_name,
        std::string_view column_name,
        const std::optional<expr::ExprNode>& where = std::nullopt
    ) const;

    GeneratedSql generate_expression(const expr::ExprNode& node) const;

private:
    std::string visit(const expr::ExprNode& node, std::vector<BoundValue>& params) const;

    const ISqlDialect& dialect_;
    mutable size_t param_counter_ = 0;
};

} // namespace cpplinq
