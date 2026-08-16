#pragma once
#include "cpplinq/core/expression.h"
#include "cpplinq/core/table.h"
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
#include <type_traits>

namespace cpplinq {

enum class JoinType {
    Inner,
    Left
};

template <typename... ColumnDefs>
inline std::vector<std::string> get_tuple_column_names(const std::tuple<ColumnDefs...>& cols) {
    std::vector<std::string> names;
    names.reserve(sizeof...(ColumnDefs));
    std::apply([&names](const auto&... c) {
        (names.emplace_back(c.name), ...);
    }, cols);
    return names;
}

template <JoinType JType, typename E1, typename E2, typename Tuple1, typename Tuple2>
class JoinedQueryBuilder {
public:
    using ResultType = std::conditional_t<
        JType == JoinType::Inner,
        std::pair<E1, E2>,
        std::pair<E1, std::optional<E2>>
    >;

    JoinedQueryBuilder(
        IConnection& conn,
        std::string primary_table,
        const Tuple1& primary_cols,
        std::string joined_table,
        const Tuple2& joined_cols,
        expr::ExprNode on_cond,
        std::optional<expr::ExprNode> where_clause = std::nullopt
    ) : conn_(conn),
        primary_table_(std::move(primary_table)),
        primary_cols_(primary_cols),
        joined_table_(std::move(joined_table)),
        joined_cols_(joined_cols),
        on_cond_(std::move(on_cond)),
        where_clause_(std::move(where_clause)) {}

    JoinedQueryBuilder& where(expr::Expr condition) {
        if (where_clause_.has_value()) {
            where_clause_ = expr::Expr(std::make_shared<expr::LogicExpr>(
                std::move(*where_clause_),
                expr::LogicOp::And,
                std::move(condition.node)
            )).node;
        } else {
            where_clause_ = std::move(condition.node);
        }
        return *this;
    }

    JoinedQueryBuilder& order_by(expr::Expr column, expr::SortDir dir = expr::SortDir::Asc) {
        order_clauses_.clear();
        order_clauses_.emplace_back(std::move(column.node), dir);
        return *this;
    }

    JoinedQueryBuilder& order_by_desc(expr::Expr column) {
        return order_by(std::move(column), expr::SortDir::Desc);
    }

    JoinedQueryBuilder& then_by(expr::Expr column, expr::SortDir dir = expr::SortDir::Asc) {
        order_clauses_.emplace_back(std::move(column.node), dir);
        return *this;
    }

    JoinedQueryBuilder& then_by_desc(expr::Expr column) {
        return then_by(std::move(column), expr::SortDir::Desc);
    }

    JoinedQueryBuilder& limit(size_t n) {
        limit_ = n;
        return *this;
    }

    JoinedQueryBuilder& offset(size_t n) {
        offset_ = n;
        return *this;
    }

    std::vector<ResultType> to_vector() {
        SqlGenerator gen(conn_.dialect());
        auto prim_names = get_tuple_column_names(primary_cols_);
        auto join_names = get_tuple_column_names(joined_cols_);

        JoinClause jc{
            JType == JoinType::Inner ? "INNER JOIN" : "LEFT JOIN",
            joined_table_,
            on_cond_
        };

        auto result = gen.generate_joined_select(
            primary_table_,
            prim_names,
            {jc},
            {{joined_table_, join_names}},
            where_clause_,
            order_clauses_,
            limit_,
            offset_
        );

        auto stmt = conn_.prepare(result.sql);
        for (size_t i = 0; i < result.params.size(); ++i) {
            stmt->bind(static_cast<int>(i), result.params[i]);
        }
        auto reader = stmt->execute_query();

        int n1 = static_cast<int>(prim_names.size());
        auto mapper1 = create_row_mapper_helper<E1>(primary_cols_, 0);
        auto mapper2 = create_row_mapper_helper<E2>(joined_cols_, n1);

        std::vector<ResultType> results;
        if (reader) {
            while (reader->next()) {
                E1 e1 = mapper1.map_row(*reader);
                if constexpr (JType == JoinType::Inner) {
                    E2 e2 = mapper2.map_row(*reader);
                    results.emplace_back(std::move(e1), std::move(e2));
                } else {
                    if (mapper2.is_all_null(*reader)) {
                        results.emplace_back(std::move(e1), std::nullopt);
                    } else {
                        results.emplace_back(std::move(e1), mapper2.map_row(*reader));
                    }
                }
            }
        }
        return results;
    }

    std::optional<ResultType> first() {
        limit_ = 1;
        auto results = to_vector();
        if (results.empty()) return std::nullopt;
        return std::move(results[0]);
    }

    size_t count() {
        auto vec = to_vector();
        return vec.size();
    }

private:
    IConnection& conn_;
    std::string primary_table_;
    Tuple1 primary_cols_;
    std::string joined_table_;
    Tuple2 joined_cols_;
    expr::ExprNode on_cond_;
    std::optional<expr::ExprNode> where_clause_;
    std::vector<std::pair<expr::ExprNode, expr::SortDir>> order_clauses_;
    std::optional<size_t> limit_;
    std::optional<size_t> offset_;

    template <typename Entity, typename... Cols>
    static auto create_row_mapper_helper(const std::tuple<Cols...>& cols, int offset) {
        return RowMapper<Entity, Cols...>(cols, offset);
    }
};

template <JoinType Type, typename PrimaryQB, typename JoinedTableDef>
class JoinProxy {
public:
    JoinProxy(PrimaryQB& qb, const JoinedTableDef& target)
        : qb_(qb), target_(target) {}

    auto on(expr::Expr condition) {
        return qb_.template make_joined<Type>(target_, std::move(condition));
    }

private:
    PrimaryQB& qb_;
    const JoinedTableDef& target_;
};

template <typename Entity, typename... ColumnDefs>
class QueryBuilder;

template <typename Entity, typename... ColumnDefs>
class SetOpQueryBuilder {
public:
    SetOpQueryBuilder(
        IConnection& conn,
        std::string base_table,
        const std::tuple<ColumnDefs...>& columns,
        std::optional<expr::ExprNode> base_where,
        bool base_distinct,
        std::vector<SetOpClause> ops
    ) : conn_(conn),
        base_table_(std::move(base_table)),
        columns_(columns),
        base_where_(std::move(base_where)),
        base_distinct_(base_distinct),
        ops_(std::move(ops)) {}

    template <typename... OtherCols>
    SetOpQueryBuilder& union_with(const QueryBuilder<Entity, OtherCols...>& other);

    template <typename... OtherCols>
    SetOpQueryBuilder& union_all(const QueryBuilder<Entity, OtherCols...>& other);

    template <typename... OtherCols>
    SetOpQueryBuilder& intersect(const QueryBuilder<Entity, OtherCols...>& other);

    template <typename... OtherCols>
    SetOpQueryBuilder& except_from(const QueryBuilder<Entity, OtherCols...>& other);

    SetOpQueryBuilder& order_by(expr::Expr column, expr::SortDir dir = expr::SortDir::Asc) {
        order_clauses_.clear();
        order_clauses_.emplace_back(std::move(column.node), dir);
        return *this;
    }

    SetOpQueryBuilder& order_by_desc(expr::Expr column) {
        return order_by(std::move(column), expr::SortDir::Desc);
    }

    SetOpQueryBuilder& then_by(expr::Expr column, expr::SortDir dir = expr::SortDir::Asc) {
        order_clauses_.emplace_back(std::move(column.node), dir);
        return *this;
    }

    SetOpQueryBuilder& then_by_desc(expr::Expr column) {
        return then_by(std::move(column), expr::SortDir::Desc);
    }

    SetOpQueryBuilder& limit(size_t n) {
        limit_ = n;
        return *this;
    }

    SetOpQueryBuilder& offset(size_t n) {
        offset_ = n;
        return *this;
    }

    std::vector<Entity> to_vector() {
        SqlGenerator gen(conn_.dialect());
        auto base_cols = get_tuple_column_names(columns_);
        auto result = gen.generate_set_operation(
            base_table_, base_cols, base_where_, base_distinct_,
            ops_, order_clauses_, limit_, offset_
        );

        auto stmt = conn_.prepare(result.sql);
        for (size_t i = 0; i < result.params.size(); ++i) {
            stmt->bind(static_cast<int>(i), result.params[i]);
        }
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

    std::optional<Entity> first() {
        limit_ = 1;
        auto results = to_vector();
        if (results.empty()) return std::nullopt;
        return std::move(results[0]);
    }

    size_t count() {
        auto vec = to_vector();
        return vec.size();
    }

private:
    IConnection& conn_;
    std::string base_table_;
    std::tuple<ColumnDefs...> columns_;
    std::optional<expr::ExprNode> base_where_;
    bool base_distinct_ = false;
    std::vector<SetOpClause> ops_;
    std::vector<std::pair<expr::ExprNode, expr::SortDir>> order_clauses_;
    std::optional<size_t> limit_;
    std::optional<size_t> offset_;
};

template <typename Entity, typename... ColumnDefs>
class QueryBuilder {
public:
    QueryBuilder(IConnection& conn, std::string table_name,
                 const std::tuple<ColumnDefs...>& columns)
        : conn_(conn), table_name_(std::move(table_name)), columns_(columns) {}

    QueryBuilder& distinct() {
        is_distinct_ = true;
        return *this;
    }

    QueryBuilder& where(expr::Expr condition) {
        where_clause_ = std::move(condition.node);
        return *this;
    }

    template <typename OtherEntity, typename... OtherCols>
    auto join(const TableDef<OtherEntity, OtherCols...>& other_table) {
        return JoinProxy<JoinType::Inner, QueryBuilder, TableDef<OtherEntity, OtherCols...>>(*this, other_table);
    }

    template <typename OtherEntity, typename... OtherCols>
    auto left_join(const TableDef<OtherEntity, OtherCols...>& other_table) {
        return JoinProxy<JoinType::Left, QueryBuilder, TableDef<OtherEntity, OtherCols...>>(*this, other_table);
    }

    template <JoinType Type, typename OtherEntity, typename... OtherCols>
    auto make_joined(const TableDef<OtherEntity, OtherCols...>& other_table, expr::Expr condition) {
        return JoinedQueryBuilder<
            Type,
            Entity,
            OtherEntity,
            std::tuple<ColumnDefs...>,
            std::tuple<OtherCols...>
        >(
            conn_,
            table_name_,
            columns_,
            std::string(other_table.name),
            other_table.columns,
            std::move(condition.node),
            where_clause_
        );
    }

    template <typename... OtherCols>
    auto union_with(const QueryBuilder<Entity, OtherCols...>& other) {
        std::vector<SetOpClause> ops;
        ops.push_back(SetOpClause{
            SetOpType::Union,
            other.table_name(),
            other.get_column_names(),
            other.where_clause(),
            other.is_distinct()
        });
        return SetOpQueryBuilder<Entity, ColumnDefs...>(
            conn_, table_name_, columns_, where_clause_, is_distinct_, std::move(ops)
        );
    }

    template <typename... OtherCols>
    auto union_all(const QueryBuilder<Entity, OtherCols...>& other) {
        std::vector<SetOpClause> ops;
        ops.push_back(SetOpClause{
            SetOpType::UnionAll,
            other.table_name(),
            other.get_column_names(),
            other.where_clause(),
            other.is_distinct()
        });
        return SetOpQueryBuilder<Entity, ColumnDefs...>(
            conn_, table_name_, columns_, where_clause_, is_distinct_, std::move(ops)
        );
    }

    template <typename... OtherCols>
    auto intersect(const QueryBuilder<Entity, OtherCols...>& other) {
        std::vector<SetOpClause> ops;
        ops.push_back(SetOpClause{
            SetOpType::Intersect,
            other.table_name(),
            other.get_column_names(),
            other.where_clause(),
            other.is_distinct()
        });
        return SetOpQueryBuilder<Entity, ColumnDefs...>(
            conn_, table_name_, columns_, where_clause_, is_distinct_, std::move(ops)
        );
    }

    template <typename... OtherCols>
    auto except_from(const QueryBuilder<Entity, OtherCols...>& other) {
        std::vector<SetOpClause> ops;
        ops.push_back(SetOpClause{
            SetOpType::Except,
            other.table_name(),
            other.get_column_names(),
            other.where_clause(),
            other.is_distinct()
        });
        return SetOpQueryBuilder<Entity, ColumnDefs...>(
            conn_, table_name_, columns_, where_clause_, is_distinct_, std::move(ops)
        );
    }

    const std::string& table_name() const { return table_name_; }
    const std::optional<expr::ExprNode>& where_clause() const { return where_clause_; }
    bool is_distinct() const { return is_distinct_; }
    std::vector<std::string> get_column_names() const {
        return get_tuple_column_names(columns_);
    }

    expr::SubqueryExpr as_subquery() const {
        return expr::SubqueryExpr(
            table_name_,
            get_column_names(),
            where_clause_ ? std::make_shared<expr::ExprNode>(*where_clause_) : nullptr,
            is_distinct_
        );
    }

    template <typename EntityType, typename ColType>
    expr::SubqueryExpr as_subquery(const ColumnDef<EntityType, ColType>& col) const {
        return expr::SubqueryExpr(
            table_name_,
            {std::string(col.name)},
            where_clause_ ? std::make_shared<expr::ExprNode>(*where_clause_) : nullptr,
            is_distinct_
        );
    }

    expr::SubqueryExpr as_subquery(const ColumnHandle& col) const {
        return expr::SubqueryExpr(
            table_name_,
            {col.ref.column_name},
            where_clause_ ? std::make_shared<expr::ExprNode>(*where_clause_) : nullptr,
            is_distinct_
        );
    }

    operator expr::Expr() const {
        return expr::Expr(std::make_shared<expr::SubqueryExpr>(as_subquery()));
    }

    QueryBuilder& order_by(expr::Expr column, expr::SortDir dir = expr::SortDir::Asc) {
        order_clauses_.clear();
        order_clauses_.emplace_back(std::move(column.node), dir);
        return *this;
    }

    QueryBuilder& order_by(expr::OrderByExpr order) {
        order_clauses_.clear();
        order_clauses_.emplace_back(std::move(order.expr), order.direction);
        return *this;
    }

    QueryBuilder& order_by_desc(expr::Expr column) {
        return order_by(std::move(column), expr::SortDir::Desc);
    }

    QueryBuilder& then_by(expr::Expr column, expr::SortDir dir = expr::SortDir::Asc) {
        order_clauses_.emplace_back(std::move(column.node), dir);
        return *this;
    }

    QueryBuilder& then_by(expr::OrderByExpr order) {
        order_clauses_.emplace_back(std::move(order.expr), order.direction);
        return *this;
    }

    QueryBuilder& then_by_desc(expr::Expr column) {
        return then_by(std::move(column), expr::SortDir::Desc);
    }

    QueryBuilder& group_by(expr::Expr column) {
        group_by_clauses_.emplace_back(std::move(column.node));
        return *this;
    }

    template <typename... MoreCols>
    QueryBuilder& group_by(expr::Expr first_col, MoreCols&&... rest) {
        group_by_clauses_.emplace_back(std::move(first_col.node));
        (group_by_clauses_.emplace_back(expr::Expr(std::forward<MoreCols>(rest)).node), ...);
        return *this;
    }

    QueryBuilder& having(expr::Expr condition) {
        having_clause_ = std::move(condition.node);
        return *this;
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
                                          order_clauses_, limit_, offset_, is_distinct_,
                                          group_by_clauses_, having_clause_);
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

    // Terminal: COUNT(DISTINCT col)
    size_t count_distinct(const expr::Expr& column) {
        std::string col_name;
        if (auto* ref = std::get_if<expr::ColumnRef>(&column.node)) {
            col_name = ref->column_name;
        }
        SqlGenerator gen(conn_.dialect());
        auto result = gen.generate_count(table_name_, where_clause_, true, col_name);
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
    bool is_distinct_ = false;
    std::vector<expr::ExprNode> group_by_clauses_;
    std::optional<expr::ExprNode> having_clause_;

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

template <typename Entity, typename... ColumnDefs>
template <typename... OtherCols>
SetOpQueryBuilder<Entity, ColumnDefs...>&
SetOpQueryBuilder<Entity, ColumnDefs...>::union_with(const QueryBuilder<Entity, OtherCols...>& other) {
    ops_.push_back(SetOpClause{
        SetOpType::Union,
        other.table_name(),
        other.get_column_names(),
        other.where_clause(),
        other.is_distinct()
    });
    return *this;
}

template <typename Entity, typename... ColumnDefs>
template <typename... OtherCols>
SetOpQueryBuilder<Entity, ColumnDefs...>&
SetOpQueryBuilder<Entity, ColumnDefs...>::union_all(const QueryBuilder<Entity, OtherCols...>& other) {
    ops_.push_back(SetOpClause{
        SetOpType::UnionAll,
        other.table_name(),
        other.get_column_names(),
        other.where_clause(),
        other.is_distinct()
    });
    return *this;
}

template <typename Entity, typename... ColumnDefs>
template <typename... OtherCols>
SetOpQueryBuilder<Entity, ColumnDefs...>&
SetOpQueryBuilder<Entity, ColumnDefs...>::intersect(const QueryBuilder<Entity, OtherCols...>& other) {
    ops_.push_back(SetOpClause{
        SetOpType::Intersect,
        other.table_name(),
        other.get_column_names(),
        other.where_clause(),
        other.is_distinct()
    });
    return *this;
}

template <typename Entity, typename... ColumnDefs>
template <typename... OtherCols>
SetOpQueryBuilder<Entity, ColumnDefs...>&
SetOpQueryBuilder<Entity, ColumnDefs...>::except_from(const QueryBuilder<Entity, OtherCols...>& other) {
    ops_.push_back(SetOpClause{
        SetOpType::Except,
        other.table_name(),
        other.get_column_names(),
        other.where_clause(),
        other.is_distinct()
    });
    return *this;
}

template <typename Entity, typename... ColumnDefs>
inline expr::Expr exists(const QueryBuilder<Entity, ColumnDefs...>& query) {
    return expr::exists(query.as_subquery());
}

template <typename Entity, typename... ColumnDefs>
inline expr::Expr not_exists(const QueryBuilder<Entity, ColumnDefs...>& query) {
    return expr::not_exists(query.as_subquery());
}

} // namespace cpplinq
