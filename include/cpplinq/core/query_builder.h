#pragma once
#include "cpplinq/core/expression.h"
#include "cpplinq/driver/connection.h"
#include "cpplinq/mapping/row_mapper.h"
#if __has_include("cpplinq/core/sql_generator.h")
#include "cpplinq/core/sql_generator.h"
#endif
#include <vector>
#include <string>
#include <optional>
#include <tuple>
#include <utility>
#include <cstddef>

namespace cpplinq {

template <typename Entity, typename... ColumnDefs>
class QueryBuilder {
public:
    QueryBuilder(IConnection& conn, std::string table_name,
                 const std::tuple<ColumnDefs...>& columns)
        : conn_(conn), table_name_(std::move(table_name)), columns_(columns) {}

    QueryBuilder& where(expr::Expr condition) {
        where_clause_ = std::move(condition.node);
        return *this;
    }

    QueryBuilder& order_by(expr::Expr column, expr::SortDir dir = expr::SortDir::Asc) {
        order_clauses_.emplace_back(std::move(column.node), dir);
        return *this;
    }

    QueryBuilder& order_by(expr::OrderByExpr order) {
        order_clauses_.emplace_back(std::move(order.expr), order.direction);
        return *this;
    }

    QueryBuilder& order_by_desc(expr::Expr column) {
        return order_by(std::move(column), expr::SortDir::Desc);
    }

    QueryBuilder& limit(size_t n) {
        limit_ = n;
        return *this;
    }

    QueryBuilder& offset(size_t n) {
        offset_ = n;
        return *this;
    }

    // Terminal: SELECT * -> vector<Entity>
    std::vector<Entity> to_vector() {
        SqlGenerator gen(conn_.dialect());
        auto col_names = get_column_names();
        auto result = gen.generate_select(table_name_, col_names, where_clause_,
                                          order_clauses_, limit_, offset_);
        auto stmt = conn_.prepare(result.sql);
        bind_params(*stmt, result.params);
        auto reader = stmt->execute_query();
        
        RowMapper<Entity, ColumnDefs...> mapper(columns_);
        std::vector<Entity> entities;
        if (reader) {
            while (reader->next()) {
                entities.push_back(mapper.map_row(*reader));
            }
        }
        return entities;
    }

    // Terminal: first result
    std::optional<Entity> first() {
        limit_ = 1;
        auto results = to_vector();
        if (results.empty()) return std::nullopt;
        return std::move(results[0]);
    }

    // Terminal: COUNT(*)
    size_t count() {
        SqlGenerator gen(conn_.dialect());
        auto result = gen.generate_count(table_name_, where_clause_);
        auto stmt = conn_.prepare(result.sql);
        bind_params(*stmt, result.params);
        auto reader = stmt->execute_query();
        if (reader && reader->next()) {
            return static_cast<size_t>(reader->get_int64(0));
        }
        return 0;
    }

    // Terminal: AVG
    std::optional<double> avg(const expr::Expr& column) {
        return aggregate_impl("AVG", column);
    }

    // Terminal: SUM
    std::optional<double> sum(const expr::Expr& column) {
        return aggregate_impl("SUM", column);
    }

    // Terminal: MIN
    std::optional<double> min_val(const expr::Expr& column) {
        return aggregate_impl("MIN", column);
    }

    // Terminal: MAX
    std::optional<double> max_val(const expr::Expr& column) {
        return aggregate_impl("MAX", column);
    }

    // Terminal: UPDATE
    size_t update(std::vector<expr::AssignExpr> assignments) {
        SqlGenerator gen(conn_.dialect());
        auto result = gen.generate_update(table_name_, assignments, where_clause_);
        auto stmt = conn_.prepare(result.sql);
        bind_params(*stmt, result.params);
        return stmt->execute_non_query();
    }

    // Terminal: DELETE
    size_t remove() {
        SqlGenerator gen(conn_.dialect());
        auto result = gen.generate_delete(table_name_, where_clause_);
        auto stmt = conn_.prepare(result.sql);
        bind_params(*stmt, result.params);
        return stmt->execute_non_query();
    }

private:
    IConnection& conn_;
    std::string table_name_;
    const std::tuple<ColumnDefs...>& columns_;
    std::optional<expr::ExprNode> where_clause_;
    std::vector<std::pair<expr::ExprNode, expr::SortDir>> order_clauses_;
    std::optional<size_t> limit_;
    std::optional<size_t> offset_;

    std::vector<std::string> get_column_names() const {
        std::vector<std::string> names;
        names.reserve(sizeof...(ColumnDefs));
        std::apply([&names](const auto&... cols) {
            (names.emplace_back(cols.name), ...);
        }, columns_);
        return names;
    }

    void bind_params(IPreparedStatement& stmt, const std::vector<BoundValue>& params) {
        for (size_t i = 0; i < params.size(); ++i) {
            stmt.bind(static_cast<int>(i), params[i]);
        }
    }

    std::optional<double> aggregate_impl(const char* func_name, const expr::Expr& column) {
        // Extract column name from the expression
        std::string col_name;
        if (auto* ref = std::get_if<expr::ColumnRef>(&column.node)) {
            col_name = ref->column_name;
        }
        SqlGenerator gen(conn_.dialect());
        auto result = gen.generate_aggregate(func_name, table_name_, col_name, where_clause_);
        auto stmt = conn_.prepare(result.sql);
        bind_params(*stmt, result.params);
        auto reader = stmt->execute_query();
        if (reader && reader->next() && !reader->is_null(0)) {
            return reader->get_double(0);
        }
        return std::nullopt;
    }
};

} // namespace cpplinq
