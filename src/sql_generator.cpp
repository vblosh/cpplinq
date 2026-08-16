#include "cpplinq/core/sql_generator.h"

#include <type_traits>

namespace cpplinq {

namespace {

BoundValue sql_value_to_bound_value(const expr::SqlValue& val) {
    return std::visit([](const auto& v) -> BoundValue {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
            return std::monostate{};
        } else if constexpr (std::is_same_v<T, int64_t>) {
            return v;
        } else if constexpr (std::is_same_v<T, double>) {
            return v;
        } else if constexpr (std::is_same_v<T, std::string>) {
            return v;
        } else if constexpr (std::is_same_v<T, bool>) {
            return v;
        }
    }, val);
}

} // namespace

SqlGenerator::SqlGenerator(const ISqlDialect& dialect)
    : dialect_(dialect)
    , param_counter_(0)
{}

std::string SqlGenerator::visit(const expr::ExprNode& node, std::vector<BoundValue>& params) const {
    return std::visit([this, &params](const auto& item) -> std::string {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, expr::ColumnRef>) {
            if (item.table_name.empty()) {
                return dialect_.quote_id(item.column_name);
            } else {
                return dialect_.quote_id(item.table_name) + "." + dialect_.quote_id(item.column_name);
            }
        } else if constexpr (std::is_same_v<T, expr::Literal>) {
            params.push_back(sql_value_to_bound_value(item.value));
            size_t idx = param_counter_++;
            return dialect_.placeholder(idx);
        } else if constexpr (std::is_same_v<T, std::shared_ptr<expr::BinaryExpr>>) {
            if (!item) return "";
            std::string op_str;
            switch (item->op) {
                case expr::CompareOp::Eq: op_str = " = "; break;
                case expr::CompareOp::Ne: op_str = " <> "; break;
                case expr::CompareOp::Lt: op_str = " < "; break;
                case expr::CompareOp::Le: op_str = " <= "; break;
                case expr::CompareOp::Gt: op_str = " > "; break;
                case expr::CompareOp::Ge: op_str = " >= "; break;
            }
            std::string left_str = visit(item->left, params);
            std::string right_str = visit(item->right, params);
            return "(" + left_str + op_str + right_str + ")";
        } else if constexpr (std::is_same_v<T, std::shared_ptr<expr::LogicExpr>>) {
            if (!item) return "";
            std::string op_str;
            switch (item->op) {
                case expr::LogicOp::And: op_str = " AND "; break;
                case expr::LogicOp::Or:  op_str = " OR "; break;
            }
            std::string left_str = visit(item->left, params);
            std::string right_str = visit(item->right, params);
            return "(" + left_str + op_str + right_str + ")";
        } else if constexpr (std::is_same_v<T, std::shared_ptr<expr::UnaryExpr>>) {
            if (!item) return "";
            switch (item->op) {
                case expr::UnaryOp::Not:
                    return "NOT (" + visit(item->operand, params) + ")";
                case expr::UnaryOp::IsNull:
                    return visit(item->operand, params) + " IS NULL";
                case expr::UnaryOp::IsNotNull:
                    return visit(item->operand, params) + " IS NOT NULL";
            }
            return "";
        } else if constexpr (std::is_same_v<T, std::shared_ptr<expr::BetweenExpr>>) {
            if (!item) return "";
            std::string expr_str = visit(item->expr, params);
            std::string low_str = visit(item->low, params);
            std::string high_str = visit(item->high, params);
            return "(" + expr_str + (item->is_not ? " NOT BETWEEN " : " BETWEEN ") + low_str + " AND " + high_str + ")";
        } else if constexpr (std::is_same_v<T, std::shared_ptr<expr::LikeExpr>>) {
            if (!item) return "";
            std::string expr_str = visit(item->expr, params);
            std::string pat_str = visit(item->pattern, params);
            return "(" + expr_str + (item->is_not ? " NOT LIKE " : " LIKE ") + pat_str + ")";
        } else if constexpr (std::is_same_v<T, std::shared_ptr<expr::InListExpr>>) {
            if (!item) return "";
            std::string expr_str = visit(item->expr, params);
            if (item->values.empty()) {
                return item->is_not ? "(1 = 1)" : "(1 = 0)";
            }
            std::string list_str;
            for (size_t i = 0; i < item->values.size(); ++i) {
                if (i > 0) list_str += ", ";
                list_str += visit(item->values[i], params);
            }
            return "(" + expr_str + (item->is_not ? " NOT IN (" : " IN (") + list_str + "))";
        } else if constexpr (std::is_same_v<T, std::shared_ptr<expr::FunctionExpr>>) {
            if (!item) return "";
            std::string sql = dialect_.function_name(item->function_name);
            sql += "(";
            for (size_t i = 0; i < item->arguments.size(); ++i) {
                if (i > 0) sql += ", ";
                sql += visit(item->arguments[i], params);
            }
            sql += ")";
            return sql;
        } else if constexpr (std::is_same_v<T, std::shared_ptr<expr::SubqueryExpr>>) {
            if (!item) return "";
            std::string sql = "(SELECT ";
            if (item->is_distinct) sql += "DISTINCT ";
            if (item->select_columns.empty()) {
                sql += "*";
            } else {
                for (size_t i = 0; i < item->select_columns.size(); ++i) {
                    if (i > 0) sql += ", ";
                    sql += dialect_.quote_id(item->select_columns[i]);
                }
            }
            sql += " FROM " + dialect_.quote_id(item->table_name);
            if (item->where) {
                sql += " WHERE " + visit(*item->where, params);
            }
            sql += ")";
            return sql;
        } else if constexpr (std::is_same_v<T, std::shared_ptr<expr::ExistsExpr>>) {
            if (!item) return "";
            std::string sql = item->is_not ? "NOT EXISTS (SELECT " : "EXISTS (SELECT ";
            if (item->subquery.is_distinct) sql += "DISTINCT ";
            if (item->subquery.select_columns.empty()) {
                sql += "1";
            } else {
                for (size_t i = 0; i < item->subquery.select_columns.size(); ++i) {
                    if (i > 0) sql += ", ";
                    sql += dialect_.quote_id(item->subquery.select_columns[i]);
                }
            }
            sql += " FROM " + dialect_.quote_id(item->subquery.table_name);
            if (item->subquery.where) {
                sql += " WHERE " + visit(*item->subquery.where, params);
            }
            sql += ")";
            return sql;
        } else if constexpr (std::is_same_v<T, std::shared_ptr<expr::InSubqueryExpr>>) {
            if (!item) return "";
            std::string expr_str = visit(item->expr, params);
            std::string sql = "(" + expr_str + (item->is_not ? " NOT IN (SELECT " : " IN (SELECT ");
            if (item->subquery.is_distinct) sql += "DISTINCT ";
            if (item->subquery.select_columns.empty()) {
                sql += "*";
            } else {
                for (size_t i = 0; i < item->subquery.select_columns.size(); ++i) {
                    if (i > 0) sql += ", ";
                    sql += dialect_.quote_id(item->subquery.select_columns[i]);
                }
            }
            sql += " FROM " + dialect_.quote_id(item->subquery.table_name);
            if (item->subquery.where) {
                sql += " WHERE " + visit(*item->subquery.where, params);
            }
            sql += "))";
            return sql;
        }
        return "";

    }, node);
}

GeneratedSql SqlGenerator::generate_select(
    std::string_view table_name,
    const std::vector<std::string>& columns,
    const std::optional<expr::ExprNode>& where,
    const std::vector<std::pair<expr::ExprNode, expr::SortDir>>& order_by,
    std::optional<size_t> limit,
    std::optional<size_t> offset,
    bool is_distinct,
    const std::vector<expr::ExprNode>& group_by,
    const std::optional<expr::ExprNode>& having
) const {
    GeneratedSql result;
    param_counter_ = 0;

    std::string sql = is_distinct ? "SELECT DISTINCT " : "SELECT ";
    if (columns.empty()) {
        sql += "*";
    } else {
        for (size_t i = 0; i < columns.size(); ++i) {
            if (i > 0) sql += ", ";
            sql += dialect_.quote_id(columns[i]);
        }
    }

    sql += " FROM ";
    sql += dialect_.quote_id(table_name);

    if (where.has_value()) {
        sql += " WHERE ";
        sql += visit(*where, result.params);
    }

    if (!group_by.empty()) {
        sql += " GROUP BY ";
        for (size_t i = 0; i < group_by.size(); ++i) {
            if (i > 0) sql += ", ";
            sql += visit(group_by[i], result.params);
        }
    }

    if (having.has_value()) {
        sql += " HAVING ";
        sql += visit(*having, result.params);
    }

    if (!order_by.empty()) {
        sql += " ORDER BY ";
        for (size_t i = 0; i < order_by.size(); ++i) {
            if (i > 0) sql += ", ";
            sql += visit(order_by[i].first, result.params);
            if (order_by[i].second == expr::SortDir::Desc) {
                sql += " DESC";
            } else {
                sql += " ASC";
            }
        }
    } else if ((limit.has_value() || offset.has_value()) && dialect_.limit_offset(limit, offset).find("OFFSET") != std::string::npos) {
        sql += " ORDER BY (SELECT NULL)";
    }

    sql += dialect_.limit_offset(limit, offset);

    result.sql = std::move(sql);
    return result;
}

GeneratedSql SqlGenerator::generate_select(
    std::string_view table_name,
    const std::vector<std::string>& columns,
    const std::optional<expr::ExprNode>& where,
    const std::vector<expr::OrderByExpr>& order_by,
    std::optional<size_t> limit,
    std::optional<size_t> offset,
    bool is_distinct,
    const std::vector<expr::ExprNode>& group_by,
    const std::optional<expr::ExprNode>& having
) const {
    std::vector<std::pair<expr::ExprNode, expr::SortDir>> pairs;
    pairs.reserve(order_by.size());
    for (const auto& item : order_by) {
        pairs.emplace_back(item.expr, item.direction);
    }
    return generate_select(table_name, columns, where, pairs, limit, offset, is_distinct, group_by, having);
}

GeneratedSql SqlGenerator::generate_joined_select(
    std::string_view primary_table,
    const std::vector<std::string>& primary_columns,
    const std::vector<JoinClause>& joins,
    const std::vector<std::pair<std::string, std::vector<std::string>>>& joined_tables_columns,
    const std::optional<expr::ExprNode>& where,
    const std::vector<std::pair<expr::ExprNode, expr::SortDir>>& order_by,
    std::optional<size_t> limit,
    std::optional<size_t> offset,
    bool is_distinct
) const {
    GeneratedSql result;
    param_counter_ = 0;

    std::string sql = is_distinct ? "SELECT DISTINCT " : "SELECT ";
    bool first_col = true;
    for (const auto& col : primary_columns) {
        if (!first_col) sql += ", ";
        first_col = false;
        sql += dialect_.quote_id(primary_table) + "." + dialect_.quote_id(col);
    }
    for (const auto& [table, cols] : joined_tables_columns) {
        for (const auto& col : cols) {
            if (!first_col) sql += ", ";
            first_col = false;
            sql += dialect_.quote_id(table) + "." + dialect_.quote_id(col);
        }
    }

    sql += " FROM ";
    sql += dialect_.quote_id(primary_table);

    for (const auto& j : joins) {
        sql += " " + j.join_type + " " + dialect_.quote_id(j.table_name) + " ON ";
        sql += visit(j.on_condition, result.params);
    }

    if (where.has_value()) {
        sql += " WHERE ";
        sql += visit(*where, result.params);
    }

    if (!order_by.empty()) {
        sql += " ORDER BY ";
        for (size_t i = 0; i < order_by.size(); ++i) {
            if (i > 0) sql += ", ";
            sql += visit(order_by[i].first, result.params);
            if (order_by[i].second == expr::SortDir::Desc) {
                sql += " DESC";
            } else {
                sql += " ASC";
            }
        }
    } else if ((limit.has_value() || offset.has_value()) && dialect_.limit_offset(limit, offset).find("OFFSET") != std::string::npos) {
        sql += " ORDER BY (SELECT NULL)";
    }

    sql += dialect_.limit_offset(limit, offset);

    result.sql = std::move(sql);
    return result;
}

GeneratedSql SqlGenerator::generate_set_operation(
    std::string_view base_table,
    const std::vector<std::string>& base_columns,
    const std::optional<expr::ExprNode>& base_where,
    bool base_distinct,
    const std::vector<SetOpClause>& operations,
    const std::vector<std::pair<expr::ExprNode, expr::SortDir>>& order_by,
    std::optional<size_t> limit,
    std::optional<size_t> offset
) const {
    GeneratedSql result;
    param_counter_ = 0;

    std::string sql = base_distinct ? "SELECT DISTINCT " : "SELECT ";
    for (size_t i = 0; i < base_columns.size(); ++i) {
        if (i > 0) sql += ", ";
        sql += dialect_.quote_id(base_columns[i]);
    }
    sql += " FROM " + dialect_.quote_id(base_table);
    if (base_where.has_value()) {
        sql += " WHERE " + visit(*base_where, result.params);
    }

    for (const auto& op : operations) {
        switch (op.op_type) {
            case SetOpType::Union:
                sql += " UNION ";
                break;
            case SetOpType::UnionAll:
                sql += " UNION ALL ";
                break;
            case SetOpType::Intersect:
                sql += " INTERSECT ";
                break;
            case SetOpType::Except:
                sql += " EXCEPT ";
                break;
        }
        sql += op.is_distinct ? "SELECT DISTINCT " : "SELECT ";
        for (size_t i = 0; i < op.columns.size(); ++i) {
            if (i > 0) sql += ", ";
            sql += dialect_.quote_id(op.columns[i]);
        }
        sql += " FROM " + dialect_.quote_id(op.table_name);
        if (op.where.has_value()) {
            sql += " WHERE " + visit(*op.where, result.params);
        }
    }

    if (!order_by.empty()) {
        sql += " ORDER BY ";
        for (size_t i = 0; i < order_by.size(); ++i) {
            if (i > 0) sql += ", ";
            if (auto* ref = std::get_if<expr::ColumnRef>(&order_by[i].first)) {
                sql += dialect_.quote_id(ref->column_name);
            } else {
                sql += visit(order_by[i].first, result.params);
            }
            if (order_by[i].second == expr::SortDir::Desc) {
                sql += " DESC";
            } else {
                sql += " ASC";
            }
        }
    } else if ((limit.has_value() || offset.has_value()) && dialect_.limit_offset(limit, offset).find("OFFSET") != std::string::npos) {
        sql += " ORDER BY (SELECT NULL)";
    }

    sql += dialect_.limit_offset(limit, offset);

    result.sql = std::move(sql);
    return result;
}

GeneratedSql SqlGenerator::generate_insert(
    std::string_view table_name,
    const std::vector<std::string>& column_names,
    const std::vector<BoundValue>& values,
    std::optional<std::string_view> returning_column
) const {
    GeneratedSql result;
    param_counter_ = 0;

    std::string sql = "INSERT INTO ";
    sql += dialect_.quote_id(table_name);

    if (!column_names.empty()) {
        sql += " (";
        for (size_t i = 0; i < column_names.size(); ++i) {
            if (i > 0) sql += ", ";
            sql += dialect_.quote_id(column_names[i]);
        }
        sql += ")";

        if (returning_column.has_value() && !returning_column->empty()) {
            std::string out_clause = dialect_.output_clause(*returning_column);
            if (!out_clause.empty()) {
                sql += out_clause;
            }
        }

        sql += " VALUES (";
        for (size_t i = 0; i < values.size(); ++i) {
            if (i > 0) sql += ", ";
            size_t idx = param_counter_++;
            sql += dialect_.placeholder(idx);
            result.params.push_back(values[i]);
        }
        sql += ")";
    }

    if (returning_column.has_value() && !returning_column->empty()) {
        std::string ret_clause = dialect_.returning_clause(*returning_column);
        if (!ret_clause.empty()) {
            sql += ret_clause;
        }
    }

    result.sql = std::move(sql);
    return result;

}

GeneratedSql SqlGenerator::generate_insert(
    std::string_view table_name,
    const std::vector<std::string>& column_names,
    const std::vector<expr::SqlValue>& values,
    std::optional<std::string_view> returning_column
) const {
    std::vector<BoundValue> bound_values;
    bound_values.reserve(values.size());
    for (const auto& val : values) {
        bound_values.push_back(sql_value_to_bound_value(val));
    }
    return generate_insert(table_name, column_names, bound_values, returning_column);
}

GeneratedSql SqlGenerator::generate_upsert(
    std::string_view table_name,
    const std::vector<std::string>& insert_columns,
    const std::vector<BoundValue>& values,
    const std::vector<std::string>& conflict_columns,
    const std::vector<std::string>& update_columns
) const {
    GeneratedSql result;
    param_counter_ = 0;

    result.sql = dialect_.generate_upsert(table_name, insert_columns, conflict_columns, update_columns);
    result.params = values;
    return result;
}

GeneratedSql SqlGenerator::generate_upsert(
    std::string_view table_name,
    const std::vector<std::string>& insert_columns,
    const std::vector<expr::SqlValue>& values,
    const std::vector<std::string>& conflict_columns,
    const std::vector<std::string>& update_columns
) const {
    std::vector<BoundValue> bound_values;
    bound_values.reserve(values.size());
    for (const auto& val : values) {
        bound_values.push_back(sql_value_to_bound_value(val));
    }
    return generate_upsert(table_name, insert_columns, bound_values, conflict_columns, update_columns);
}

GeneratedSql SqlGenerator::generate_update(
    std::string_view table_name,
    const std::vector<expr::AssignExpr>& assignments,
    const std::optional<expr::ExprNode>& where
) const {
    GeneratedSql result;
    param_counter_ = 0;

    std::string sql = "UPDATE ";
    sql += dialect_.quote_id(table_name);
    sql += " SET ";

    for (size_t i = 0; i < assignments.size(); ++i) {
        if (i > 0) sql += ", ";
        std::string col_name = assignments[i].column.column_name;
        if (col_name.empty()) {
            col_name = assignments[i].column.table_name;
        }
        sql += dialect_.quote_id(col_name);
        sql += " = ";
        sql += visit(assignments[i].value, result.params);
    }

    if (where.has_value()) {
        sql += " WHERE ";
        sql += visit(*where, result.params);
    }

    result.sql = std::move(sql);
    return result;
}

GeneratedSql SqlGenerator::generate_update(
    std::string_view table_name,
    const std::vector<std::pair<std::string, BoundValue>>& assignments,
    const std::optional<expr::ExprNode>& where
) const {
    GeneratedSql result;
    param_counter_ = 0;

    std::string sql = "UPDATE ";
    sql += dialect_.quote_id(table_name);
    sql += " SET ";

    for (size_t i = 0; i < assignments.size(); ++i) {
        if (i > 0) sql += ", ";
        sql += dialect_.quote_id(assignments[i].first);
        sql += " = ";
        size_t idx = param_counter_++;
        sql += dialect_.placeholder(idx);
        result.params.push_back(assignments[i].second);
    }

    if (where.has_value()) {
        sql += " WHERE ";
        sql += visit(*where, result.params);
    }

    result.sql = std::move(sql);
    return result;
}

GeneratedSql SqlGenerator::generate_delete(
    std::string_view table_name,
    const std::optional<expr::ExprNode>& where
) const {
    GeneratedSql result;
    param_counter_ = 0;

    std::string sql = "DELETE FROM ";
    sql += dialect_.quote_id(table_name);

    if (where.has_value()) {
        sql += " WHERE ";
        sql += visit(*where, result.params);
    }

    result.sql = std::move(sql);
    return result;
}

GeneratedSql SqlGenerator::generate_create_table(
    std::string_view table_name,
    const std::vector<ColumnInfo>& column_infos
) const {
    GeneratedSql result;
    param_counter_ = 0;

    std::string sql = dialect_.create_table_prefix(table_name);
    sql += " (";

    for (size_t i = 0; i < column_infos.size(); ++i) {
        if (i > 0) sql += ", ";
        const auto& col = column_infos[i];
        if (col.is_auto_increment) {
            sql += dialect_.quote_id(col.name) + " " + dialect_.auto_increment_type();
            if (col.is_unique) {
                sql += " UNIQUE";
            }
        } else {
            sql += dialect_.quote_id(col.name) + " " + dialect_.type_name(col.sql_type);
            if (col.is_primary_key) {
                sql += " PRIMARY KEY";
            }
            if (col.is_not_null) {
                sql += " NOT NULL";
            }
            if (col.is_unique) {
                sql += " UNIQUE";
            }
        }
    }

    sql += ")";

    result.sql = std::move(sql);
    return result;
}

GeneratedSql SqlGenerator::generate_count(
    std::string_view table_name,
    const std::optional<expr::ExprNode>& where,
    bool is_distinct,
    std::optional<std::string_view> distinct_column
) const {
    GeneratedSql result;
    param_counter_ = 0;

    std::string sql = "SELECT COUNT(";
    if (is_distinct && distinct_column.has_value()) {
        sql += "DISTINCT " + dialect_.quote_id(*distinct_column);
    } else {
        sql += "*";
    }
    sql += ") FROM ";
    sql += dialect_.quote_id(table_name);

    if (where.has_value()) {
        sql += " WHERE ";
        sql += visit(*where, result.params);
    }

    result.sql = std::move(sql);
    return result;
}

GeneratedSql SqlGenerator::generate_aggregate(
    std::string_view function_name,
    std::string_view table_name,
    std::string_view column_name,
    const std::optional<expr::ExprNode>& where
) const {
    GeneratedSql result;
    param_counter_ = 0;

    std::string sql = "SELECT ";
    sql += std::string(function_name);
    sql += "(";
    if (column_name == "*") {
        sql += "*";
    } else {
        sql += dialect_.quote_id(column_name);
    }
    sql += ") FROM ";
    sql += dialect_.quote_id(table_name);

    if (where.has_value()) {
        sql += " WHERE ";
        sql += visit(*where, result.params);
    }

    result.sql = std::move(sql);
    return result;
}

} // namespace cpplinq
