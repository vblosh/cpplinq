#include <gtest/gtest.h>
#include "cpplinq/core/expression.h"
#include <iostream>
#include <memory>
#include <type_traits>

using namespace cpplinq;
using namespace cpplinq::expr;

// ============================================================================
// ColumnRef & ColumnHandle Tests
// ============================================================================

TEST(ExpressionTest, ColumnRefCreationWithTable) {
    ColumnHandle col("users", "age");
    Expr e = col;
    auto* ref = std::get_if<ColumnRef>(&e.node);
    ASSERT_NE(ref, nullptr);
    EXPECT_EQ(ref->table_name, "users");
    EXPECT_EQ(ref->column_name, "age");
}

TEST(ExpressionTest, ColumnRefCreationWithoutTable) {
    ColumnHandle col("name");
    Expr e = col;
    auto* ref = std::get_if<ColumnRef>(&e.node);
    ASSERT_NE(ref, nullptr);
    EXPECT_TRUE(ref->table_name.empty());
    EXPECT_EQ(ref->column_name, "name");
}

TEST(ExpressionTest, ColumnRefEquality) {
    ColumnRef ref1("users", "id");
    ColumnRef ref2("users", "id");
    ColumnRef ref3("posts", "id");
    EXPECT_EQ(ref1, ref2);
    EXPECT_NE(ref1, ref3);
}

// ============================================================================
// Literal Construction Tests
// ============================================================================

TEST(ExpressionTest, LiteralDefaultConstructor) {
    Expr e;
    auto* lit = std::get_if<Literal>(&e.node);
    ASSERT_NE(lit, nullptr);
    EXPECT_TRUE(std::holds_alternative<std::monostate>(lit->value));
}

TEST(ExpressionTest, LiteralInt) {
    Expr e(42);
    auto* lit = std::get_if<Literal>(&e.node);
    ASSERT_NE(lit, nullptr);
    auto* val = std::get_if<int64_t>(&lit->value);
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(*val, 42);
}

TEST(ExpressionTest, LiteralInt64) {
    int64_t big = 9876543210LL;
    Expr e(big);
    auto* lit = std::get_if<Literal>(&e.node);
    ASSERT_NE(lit, nullptr);
    auto* val = std::get_if<int64_t>(&lit->value);
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(*val, big);
}

TEST(ExpressionTest, LiteralDouble) {
    Expr e(3.14159);
    auto* lit = std::get_if<Literal>(&e.node);
    ASSERT_NE(lit, nullptr);
    auto* val = std::get_if<double>(&lit->value);
    ASSERT_NE(val, nullptr);
    EXPECT_DOUBLE_EQ(*val, 3.14159);
}

TEST(ExpressionTest, LiteralFloat) {
    Expr e(2.5f);
    auto* lit = std::get_if<Literal>(&e.node);
    ASSERT_NE(lit, nullptr);
    auto* val = std::get_if<double>(&lit->value);
    ASSERT_NE(val, nullptr);
    EXPECT_DOUBLE_EQ(*val, 2.5);
}

TEST(ExpressionTest, LiteralCString) {
    Expr e("hello");
    auto* lit = std::get_if<Literal>(&e.node);
    ASSERT_NE(lit, nullptr);
    auto* val = std::get_if<std::string>(&lit->value);
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(*val, "hello");
}

TEST(ExpressionTest, LiteralStdString) {
    std::string s = "cpplinq";
    Expr e(s);
    auto* lit = std::get_if<Literal>(&e.node);
    ASSERT_NE(lit, nullptr);
    auto* val = std::get_if<std::string>(&lit->value);
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(*val, "cpplinq");
}

TEST(ExpressionTest, LiteralStringView) {
    std::string_view sv = "string_view_test";
    Expr e(sv);
    auto* lit = std::get_if<Literal>(&e.node);
    ASSERT_NE(lit, nullptr);
    auto* val = std::get_if<std::string>(&lit->value);
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(*val, "string_view_test");
}

TEST(ExpressionTest, LiteralBool) {
    Expr e_true(true);
    auto* lit_t = std::get_if<Literal>(&e_true.node);
    ASSERT_NE(lit_t, nullptr);
    auto* val_t = std::get_if<bool>(&lit_t->value);
    ASSERT_NE(val_t, nullptr);
    EXPECT_TRUE(*val_t);

    Expr e_false(false);
    auto* lit_f = std::get_if<Literal>(&e_false.node);
    ASSERT_NE(lit_f, nullptr);
    auto* val_f = std::get_if<bool>(&lit_f->value);
    ASSERT_NE(val_f, nullptr);
    EXPECT_FALSE(*val_f);
}

TEST(ExpressionTest, LiteralNullOpt) {
    Expr e(std::nullopt);
    auto* lit = std::get_if<Literal>(&e.node);
    ASSERT_NE(lit, nullptr);
    EXPECT_TRUE(std::holds_alternative<std::monostate>(lit->value));
}

TEST(ExpressionTest, LiteralOptionalWithValue) {
    std::optional<std::string> opt_str = "present";
    Expr e(opt_str);
    auto* lit = std::get_if<Literal>(&e.node);
    ASSERT_NE(lit, nullptr);
    auto* val = std::get_if<std::string>(&lit->value);
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(*val, "present");
}

TEST(ExpressionTest, LiteralOptionalWithoutValue) {
    std::optional<std::string> opt_empty = std::nullopt;
    Expr e(opt_empty);
    auto* lit = std::get_if<Literal>(&e.node);
    ASSERT_NE(lit, nullptr);
    EXPECT_TRUE(std::holds_alternative<std::monostate>(lit->value));
}

// ============================================================================
// Binary Comparison Operator Tests
// ============================================================================

TEST(ExpressionTest, BinaryEqualExpr) {
    ColumnHandle col("users", "age");
    Expr e = Expr(col) == 25;
    auto* bin = std::get_if<std::shared_ptr<BinaryExpr>>(&e.node);
    ASSERT_NE(bin, nullptr);
    EXPECT_EQ((*bin)->op, CompareOp::Eq);

    auto* left_col = std::get_if<ColumnRef>(&(*bin)->left);
    ASSERT_NE(left_col, nullptr);
    EXPECT_EQ(left_col->column_name, "age");

    auto* right_lit = std::get_if<Literal>(&(*bin)->right);
    ASSERT_NE(right_lit, nullptr);
    auto* val = std::get_if<int64_t>(&right_lit->value);
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(*val, 25);
}

TEST(ExpressionTest, BinaryNotEqualExpr) {
    ColumnHandle col("users", "status");
    Expr e = Expr(col) != "inactive";
    auto* bin = std::get_if<std::shared_ptr<BinaryExpr>>(&e.node);
    ASSERT_NE(bin, nullptr);
    EXPECT_EQ((*bin)->op, CompareOp::Ne);
}

TEST(ExpressionTest, BinaryLessThanExpr) {
    ColumnHandle col("products", "price");
    Expr e = Expr(col) < 100.0;
    auto* bin = std::get_if<std::shared_ptr<BinaryExpr>>(&e.node);
    ASSERT_NE(bin, nullptr);
    EXPECT_EQ((*bin)->op, CompareOp::Lt);
}

TEST(ExpressionTest, BinaryLessEqualExpr) {
    ColumnHandle col("products", "price");
    Expr e = Expr(col) <= 50.0;
    auto* bin = std::get_if<std::shared_ptr<BinaryExpr>>(&e.node);
    ASSERT_NE(bin, nullptr);
    EXPECT_EQ((*bin)->op, CompareOp::Le);
}

TEST(ExpressionTest, BinaryGreaterThanExpr) {
    ColumnHandle col("users", "age");
    Expr e = Expr(col) > 18;
    auto* bin = std::get_if<std::shared_ptr<BinaryExpr>>(&e.node);
    ASSERT_NE(bin, nullptr);
    EXPECT_EQ((*bin)->op, CompareOp::Gt);
}

TEST(ExpressionTest, BinaryGreaterEqualExpr) {
    ColumnHandle col("users", "score");
    Expr e = Expr(col) >= 90;
    auto* bin = std::get_if<std::shared_ptr<BinaryExpr>>(&e.node);
    ASSERT_NE(bin, nullptr);
    EXPECT_EQ((*bin)->op, CompareOp::Ge);
}

// ============================================================================
// Logical Operator Tests (AND, OR)
// ============================================================================

TEST(ExpressionTest, LogicAndExpr) {
    ColumnHandle age("users", "age");
    ColumnHandle name("users", "name");
    Expr e = (Expr(age) > 18) && (Expr(name) == "Alice");
    auto* logic = std::get_if<std::shared_ptr<LogicExpr>>(&e.node);
    ASSERT_NE(logic, nullptr);
    EXPECT_EQ((*logic)->op, LogicOp::And);

    auto* left_bin = std::get_if<std::shared_ptr<BinaryExpr>>(&(*logic)->left);
    ASSERT_NE(left_bin, nullptr);
    EXPECT_EQ((*left_bin)->op, CompareOp::Gt);

    auto* right_bin = std::get_if<std::shared_ptr<BinaryExpr>>(&(*logic)->right);
    ASSERT_NE(right_bin, nullptr);
    EXPECT_EQ((*right_bin)->op, CompareOp::Eq);
}

TEST(ExpressionTest, LogicOrExpr) {
    ColumnHandle age("users", "age");
    ColumnHandle name("users", "name");
    Expr e = (Expr(age) < 18) || (Expr(name) != "Bob");
    auto* logic = std::get_if<std::shared_ptr<LogicExpr>>(&e.node);
    ASSERT_NE(logic, nullptr);
    EXPECT_EQ((*logic)->op, LogicOp::Or);
}

TEST(ExpressionTest, LogicChainedExpr) {
    ColumnHandle a("t", "a");
    ColumnHandle b("t", "b");
    ColumnHandle c("t", "c");
    Expr e = ((Expr(a) == 1) && (Expr(b) == 2)) || (Expr(c) == 3);
    auto* root_or = std::get_if<std::shared_ptr<LogicExpr>>(&e.node);
    ASSERT_NE(root_or, nullptr);
    EXPECT_EQ((*root_or)->op, LogicOp::Or);

    auto* left_and = std::get_if<std::shared_ptr<LogicExpr>>(&(*root_or)->left);
    ASSERT_NE(left_and, nullptr);
    EXPECT_EQ((*left_and)->op, LogicOp::And);
}

// ============================================================================
// Unary Operator Tests (NOT, IS NULL, IS NOT NULL)
// ============================================================================

TEST(ExpressionTest, UnaryNotExpr) {
    ColumnHandle active("users", "is_active");
    Expr e = !Expr(active);
    auto* un = std::get_if<std::shared_ptr<UnaryExpr>>(&e.node);
    ASSERT_NE(un, nullptr);
    EXPECT_EQ((*un)->op, UnaryOp::Not);

    auto* col = std::get_if<ColumnRef>(&(*un)->operand);
    ASSERT_NE(col, nullptr);
    EXPECT_EQ(col->column_name, "is_active");
}

TEST(ExpressionTest, UnaryIsNullMethod) {
    ColumnHandle email("users", "email");
    Expr e = email.is_null();
    auto* un = std::get_if<std::shared_ptr<UnaryExpr>>(&e.node);
    ASSERT_NE(un, nullptr);
    EXPECT_EQ((*un)->op, UnaryOp::IsNull);
}

TEST(ExpressionTest, UnaryIsNotNullMethod) {
    ColumnHandle email("users", "email");
    Expr e = email.is_not_null();
    auto* un = std::get_if<std::shared_ptr<UnaryExpr>>(&e.node);
    ASSERT_NE(un, nullptr);
    EXPECT_EQ((*un)->op, UnaryOp::IsNotNull);
}

TEST(ExpressionTest, UnaryIsNullFreeFunction) {
    ColumnHandle email("users", "email");
    Expr e = is_null(Expr(email));
    auto* un = std::get_if<std::shared_ptr<UnaryExpr>>(&e.node);
    ASSERT_NE(un, nullptr);
    EXPECT_EQ((*un)->op, UnaryOp::IsNull);
}

TEST(ExpressionTest, UnaryIsNotNullFreeFunction) {
    ColumnHandle email("users", "email");
    Expr e = is_not_null(Expr(email));
    auto* un = std::get_if<std::shared_ptr<UnaryExpr>>(&e.node);
    ASSERT_NE(un, nullptr);
    EXPECT_EQ((*un)->op, UnaryOp::IsNotNull);
}

// ============================================================================
// AssignExpr Tests
// ============================================================================

TEST(ExpressionTest, AssignExprLiteral) {
    ColumnHandle col("users", "name");
    AssignExpr assign = (col = "NewName");
    EXPECT_EQ(assign.column.table_name, "users");
    EXPECT_EQ(assign.column.column_name, "name");

    auto* lit = std::get_if<Literal>(&assign.value);
    ASSERT_NE(lit, nullptr);
    auto* val = std::get_if<std::string>(&lit->value);
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(*val, "NewName");
}

TEST(ExpressionTest, AssignExprColumnHandle) {
    ColumnHandle col1("users", "backup_email");
    ColumnHandle col2("users", "primary_email");
    AssignExpr assign = (col1 = col2);
    EXPECT_EQ(assign.column.column_name, "backup_email");

    auto* ref = std::get_if<ColumnRef>(&assign.value);
    ASSERT_NE(ref, nullptr);
    EXPECT_EQ(ref->column_name, "primary_email");
}

TEST(ExpressionTest, AssignExprExplicitExpr) {
    ColumnHandle col("users", "age");
    Expr e(30);
    AssignExpr assign = (col = e);
    EXPECT_EQ(assign.column.column_name, "age");

    auto* lit = std::get_if<Literal>(&assign.value);
    ASSERT_NE(lit, nullptr);
    auto* val = std::get_if<int64_t>(&lit->value);
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(*val, 30);
}

// ============================================================================
// OrderByExpr Tests
// ============================================================================

TEST(ExpressionTest, OrderByAsc) {
    ColumnHandle col("users", "created_at");
    OrderByExpr order = asc(col);
    EXPECT_EQ(order.direction, SortDir::Asc);
    auto* ref = std::get_if<ColumnRef>(&order.expr);
    ASSERT_NE(ref, nullptr);
    EXPECT_EQ(ref->column_name, "created_at");
}

TEST(ExpressionTest, OrderByDesc) {
    ColumnHandle col("users", "score");
    OrderByExpr order = desc(col);
    EXPECT_EQ(order.direction, SortDir::Desc);
    auto* ref = std::get_if<ColumnRef>(&order.expr);
    ASSERT_NE(ref, nullptr);
    EXPECT_EQ(ref->column_name, "score");
}

TEST(ExpressionTest, BetweenAndNotBetween) {
    ColumnHandle col("users", "age");
    Expr b = col.between(18, 65);
    auto* between_ptr = std::get_if<std::shared_ptr<BetweenExpr>>(&b.node);
    ASSERT_NE(between_ptr, nullptr);
    EXPECT_FALSE((*between_ptr)->is_not);

    Expr nb = col.not_between(18, 65);
    auto* not_between_ptr = std::get_if<std::shared_ptr<BetweenExpr>>(&nb.node);
    ASSERT_NE(not_between_ptr, nullptr);
    EXPECT_TRUE((*not_between_ptr)->is_not);
}

TEST(ExpressionTest, LikeAndNotLike) {
    ColumnHandle col("users", "name");
    Expr l = col.like("Alice%");
    auto* like_ptr = std::get_if<std::shared_ptr<LikeExpr>>(&l.node);
    ASSERT_NE(like_ptr, nullptr);
    EXPECT_FALSE((*like_ptr)->is_not);

    Expr nl = col.not_like("%Bob%");
    auto* not_like_ptr = std::get_if<std::shared_ptr<LikeExpr>>(&nl.node);
    ASSERT_NE(not_like_ptr, nullptr);
    EXPECT_TRUE((*not_like_ptr)->is_not);
}

TEST(ExpressionTest, InListAndNotInList) {
    ColumnHandle col("users", "id");
    Expr in_expr = col.in_list({1, 2, 3});
    auto* in_ptr = std::get_if<std::shared_ptr<InListExpr>>(&in_expr.node);
    ASSERT_NE(in_ptr, nullptr);
    EXPECT_FALSE((*in_ptr)->is_not);
    EXPECT_EQ((*in_ptr)->values.size(), 3);

    Expr not_in_expr = col.not_in_list({4, 5});
    auto* not_in_ptr = std::get_if<std::shared_ptr<InListExpr>>(&not_in_expr.node);
    ASSERT_NE(not_in_ptr, nullptr);
    EXPECT_TRUE((*not_in_ptr)->is_not);
    EXPECT_EQ((*not_in_ptr)->values.size(), 2);
}

TEST(ExpressionTest, SqlFunctionsAst) {
    ColumnHandle name("users", "name");
    ColumnHandle email("users", "email");
    ColumnHandle score("users", "score");

    Expr l = lower(name);
    auto* func_l = std::get_if<std::shared_ptr<FunctionExpr>>(&l.node);
    ASSERT_NE(func_l, nullptr);
    EXPECT_EQ((*func_l)->function_name, "LOWER");
    EXPECT_EQ((*func_l)->arguments.size(), 1);

    Expr u = upper(name);
    auto* func_u = std::get_if<std::shared_ptr<FunctionExpr>>(&u.node);
    ASSERT_NE(func_u, nullptr);
    EXPECT_EQ((*func_u)->function_name, "UPPER");

    Expr len = length(name);
    auto* func_len = std::get_if<std::shared_ptr<FunctionExpr>>(&len.node);
    ASSERT_NE(func_len, nullptr);
    EXPECT_EQ((*func_len)->function_name, "LENGTH");

    Expr sub = substr(name, 1, 3);
    auto* func_sub = std::get_if<std::shared_ptr<FunctionExpr>>(&sub.node);
    ASSERT_NE(func_sub, nullptr);
    EXPECT_EQ((*func_sub)->function_name, "SUBSTR");
    EXPECT_EQ((*func_sub)->arguments.size(), 3);

    Expr coal = coalesce(email, "default@test.com");
    auto* func_coal = std::get_if<std::shared_ptr<FunctionExpr>>(&coal.node);
    ASSERT_NE(func_coal, nullptr);
    EXPECT_EQ((*func_coal)->function_name, "COALESCE");
    EXPECT_EQ((*func_coal)->arguments.size(), 2);
}

TEST(ExpressionTest, ExprNullptrCString) {
    const char* null_str = nullptr;
    Expr e(null_str);
    auto* lit = std::get_if<Literal>(&e.node);
    ASSERT_NE(lit, nullptr);
    auto* val = std::get_if<std::string>(&lit->value);
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(*val, "");
}

TEST(ExpressionTest, ColumnHandleToString) {
    ColumnHandle col("users", "email");
    std::string str = col;
    EXPECT_EQ(str, "email");
}

// ============================================================================
// Current date/time expression nodes
// ============================================================================

// The documented API exposes current_timestamp_val() and current_date_val().
// The absence of now(), current_date(), and current_timestamp() aliases is a
// compile-time property: only the factories below are declared, so the static
// assertions pin the supported API used by the runtime tests.
static_assert(std::is_same_v<decltype(cpplinq::expr::current_timestamp_val()), cpplinq::expr::Expr>);
static_assert(std::is_same_v<decltype(cpplinq::expr::current_date_val()), cpplinq::expr::Expr>);

TEST(ExpressionTest, CurrentTimestampExprNode) {
    Expr e = current_timestamp_val();

    ASSERT_TRUE(std::holds_alternative<CurrentTimestampExpr>(e.node));
    EXPECT_NE(std::get_if<CurrentTimestampExpr>(&e.node), nullptr);
    EXPECT_EQ(std::get_if<CurrentDateExpr>(&e.node), nullptr);
}

TEST(ExpressionTest, CurrentDateExprNode) {
    Expr e = current_date_val();

    ASSERT_TRUE(std::holds_alternative<CurrentDateExpr>(e.node));
    EXPECT_NE(std::get_if<CurrentDateExpr>(&e.node), nullptr);
    EXPECT_EQ(std::get_if<CurrentTimestampExpr>(&e.node), nullptr);
}

TEST(ExpressionTest, CurrentTimestampUsableInComparisonAndDateAdd) {
    ColumnHandle created_at("events", "created_at");

    Expr comparison = current_timestamp_val() >= created_at;
    auto* binary = std::get_if<std::shared_ptr<BinaryExpr>>(&comparison.node);
    ASSERT_NE(binary, nullptr);
    EXPECT_EQ((*binary)->op, CompareOp::Ge);
    EXPECT_NE(std::get_if<CurrentTimestampExpr>(&(*binary)->left), nullptr);
    auto* right_col = std::get_if<ColumnRef>(&(*binary)->right);
    ASSERT_NE(right_col, nullptr);
    EXPECT_EQ(right_col->column_name, "created_at");

    Expr added = date_add_days(current_timestamp_val(), 7);
    auto* date_add = std::get_if<std::shared_ptr<DateAddExpr>>(&added.node);
    ASSERT_NE(date_add, nullptr);
    EXPECT_NE(std::get_if<CurrentTimestampExpr>(&(*date_add)->expr), nullptr);
}

// ============================================================================
// Date extraction and arithmetic expression nodes
// ============================================================================

namespace {

void expect_extract_expr(const Expr& e, DatePart expected_part,
                         const std::string& expected_table,
                         const std::string& expected_column) {
    auto* extract = std::get_if<std::shared_ptr<ExtractExpr>>(&e.node);
    ASSERT_NE(extract, nullptr);
    EXPECT_EQ((*extract)->part, expected_part);

    auto* column = std::get_if<ColumnRef>(&(*extract)->expr);
    ASSERT_NE(column, nullptr);
    EXPECT_EQ(column->table_name, expected_table);
    EXPECT_EQ(column->column_name, expected_column);
}

} // namespace

TEST(ExpressionTest, ExtractDatePartMethodsAst) {
    ColumnHandle created_at("events", "created_at");

    expect_extract_expr(created_at.year(), DatePart::Year, "events", "created_at");
    expect_extract_expr(created_at.month(), DatePart::Month, "events", "created_at");
    expect_extract_expr(created_at.day(), DatePart::Day, "events", "created_at");
}

TEST(ExpressionTest, ExtractDatePartFreeFunctionsAst) {
    ColumnHandle created_at("events", "created_at");

    expect_extract_expr(extract_year(created_at), DatePart::Year, "events", "created_at");
    expect_extract_expr(extract_month(created_at), DatePart::Month, "events", "created_at");
    expect_extract_expr(extract_day(created_at), DatePart::Day, "events", "created_at");
}

TEST(ExpressionTest, DateAddDaysMethodAst) {
    ColumnHandle created_at("events", "created_at");

    Expr e = created_at.add_days(7);
    auto* date_add = std::get_if<std::shared_ptr<DateAddExpr>>(&e.node);
    ASSERT_NE(date_add, nullptr);

    auto* column = std::get_if<ColumnRef>(&(*date_add)->expr);
    ASSERT_NE(column, nullptr);
    EXPECT_EQ(column->table_name, "events");
    EXPECT_EQ(column->column_name, "created_at");

    auto* literal = std::get_if<Literal>(&(*date_add)->days);
    ASSERT_NE(literal, nullptr);
    auto* days = std::get_if<int64_t>(&literal->value);
    ASSERT_NE(days, nullptr);
    EXPECT_EQ(*days, 7);
}

TEST(ExpressionTest, DateAddDaysFreeFunctionAst) {
    ColumnHandle created_at("events", "created_at");
    ColumnHandle offset_days("events", "offset_days");

    Expr positive = date_add_days(created_at, 7);
    auto* date_add = std::get_if<std::shared_ptr<DateAddExpr>>(&positive.node);
    ASSERT_NE(date_add, nullptr);
    auto* literal = std::get_if<Literal>(&(*date_add)->days);
    ASSERT_NE(literal, nullptr);
    auto* positive_days = std::get_if<int64_t>(&literal->value);
    ASSERT_NE(positive_days, nullptr);
    EXPECT_EQ(*positive_days, 7);

    Expr negative = date_add_days(created_at, -3);
    auto* negative_add = std::get_if<std::shared_ptr<DateAddExpr>>(&negative.node);
    ASSERT_NE(negative_add, nullptr);
    auto* negative_literal = std::get_if<Literal>(&(*negative_add)->days);
    ASSERT_NE(negative_literal, nullptr);
    auto* negative_days = std::get_if<int64_t>(&negative_literal->value);
    ASSERT_NE(negative_days, nullptr);
    EXPECT_EQ(*negative_days, -3);

    Expr column_days = date_add_days(created_at, offset_days);
    auto* column_add = std::get_if<std::shared_ptr<DateAddExpr>>(&column_days.node);
    ASSERT_NE(column_add, nullptr);
    auto* days_ref = std::get_if<ColumnRef>(&(*column_add)->days);
    ASSERT_NE(days_ref, nullptr);
    EXPECT_EQ(days_ref->column_name, "offset_days");
}

TEST(ExpressionTest, DateAddDaysCanBeNested) {
    ColumnHandle created_at("events", "created_at");

    Expr nested = date_add_days(created_at, 7).add_days(1);
    auto* outer = std::get_if<std::shared_ptr<DateAddExpr>>(&nested.node);
    ASSERT_NE(outer, nullptr);

    auto* inner = std::get_if<std::shared_ptr<DateAddExpr>>(&(*outer)->expr);
    ASSERT_NE(inner, nullptr);
    auto* column = std::get_if<ColumnRef>(&(*inner)->expr);
    ASSERT_NE(column, nullptr);
    EXPECT_EQ(column->column_name, "created_at");
}

// ============================================================================
// Subquery expression wrappers and convertibility
// ============================================================================

static_assert(!std::is_convertible_v<SubqueryExpr, Expr>);
static_assert(std::is_constructible_v<Expr, std::shared_ptr<SubqueryExpr>>);

TEST(ExpressionTest, SubqueryExprStoresWhereClause) {
    ColumnHandle amount("orders", "amount");
    Expr where = amount > 100.0;

    SubqueryExpr sub("orders", {"user_id"}, where.node);
    EXPECT_EQ(sub.table_name, "orders");
    ASSERT_EQ(sub.select_columns.size(), 1);
    EXPECT_EQ(sub.select_columns[0], "user_id");
    ASSERT_NE(sub.where, nullptr);
    EXPECT_TRUE(std::holds_alternative<std::shared_ptr<BinaryExpr>>(*sub.where));
    EXPECT_FALSE(sub.is_distinct);

    SubqueryExpr no_where("orders", {"amount"}, nullptr, true);
    EXPECT_EQ(no_where.where, nullptr);
    EXPECT_TRUE(no_where.is_distinct);
}

TEST(ExpressionTest, SubqueryExprRequiresExplicitExprWrap) {
    SubqueryExpr sub("orders", {"amount"});
    Expr wrapped(std::make_shared<SubqueryExpr>(sub));

    auto* stored = std::get_if<std::shared_ptr<SubqueryExpr>>(&wrapped.node);
    ASSERT_NE(stored, nullptr);
    EXPECT_EQ((*stored)->table_name, "orders");
    ASSERT_EQ((*stored)->select_columns.size(), 1);
    EXPECT_EQ((*stored)->select_columns[0], "amount");
}

TEST(ExpressionTest, SubqueryInAndExistsWrappers) {
    ColumnHandle user_id("users", "id");
    ColumnHandle order_user_id("orders", "user_id");
    SubqueryExpr sub("orders", {"user_id"}, (order_user_id == user_id).node);

    Expr in_expr = user_id.in(sub);
    auto* in = std::get_if<std::shared_ptr<InSubqueryExpr>>(&in_expr.node);
    ASSERT_NE(in, nullptr);
    EXPECT_FALSE((*in)->is_not);
    auto* in_column = std::get_if<ColumnRef>(&(*in)->expr);
    ASSERT_NE(in_column, nullptr);
    EXPECT_EQ(in_column->column_name, "id");
    EXPECT_EQ((*in)->subquery.table_name, "orders");
    ASSERT_EQ((*in)->subquery.select_columns.size(), 1);
    EXPECT_EQ((*in)->subquery.select_columns[0], "user_id");

    Expr not_in_expr = user_id.not_in(sub);
    auto* not_in = std::get_if<std::shared_ptr<InSubqueryExpr>>(&not_in_expr.node);
    ASSERT_NE(not_in, nullptr);
    EXPECT_TRUE((*not_in)->is_not);

    Expr exists_expr = exists(sub);
    auto* exists_node = std::get_if<std::shared_ptr<ExistsExpr>>(&exists_expr.node);
    ASSERT_NE(exists_node, nullptr);
    EXPECT_FALSE((*exists_node)->is_not);
    EXPECT_EQ((*exists_node)->subquery.table_name, "orders");

    Expr not_exists_expr = not_exists(sub);
    auto* not_exists_node = std::get_if<std::shared_ptr<ExistsExpr>>(&not_exists_expr.node);
    ASSERT_NE(not_exists_node, nullptr);
    EXPECT_TRUE((*not_exists_node)->is_not);
}

// ============================================================================
// Remaining documented scalar function nodes
// ============================================================================

TEST(ExpressionTest, TrimAbsAndRoundAst) {
    ColumnHandle name("users", "name");
    ColumnHandle balance("accounts", "balance");
    ColumnHandle amount("orders", "amount");

    Expr trimmed = trim(name);
    auto* trim_func = std::get_if<std::shared_ptr<FunctionExpr>>(&trimmed.node);
    ASSERT_NE(trim_func, nullptr);
    EXPECT_EQ((*trim_func)->function_name, "TRIM");
    EXPECT_EQ((*trim_func)->arguments.size(), 1);

    Expr absolute = abs_val(balance);
    auto* abs_func = std::get_if<std::shared_ptr<FunctionExpr>>(&absolute.node);
    ASSERT_NE(abs_func, nullptr);
    EXPECT_EQ((*abs_func)->function_name, "ABS");

    Expr rounded = round_val(amount);
    auto* round_func = std::get_if<std::shared_ptr<FunctionExpr>>(&rounded.node);
    ASSERT_NE(round_func, nullptr);
    EXPECT_EQ((*round_func)->function_name, "ROUND");
    ASSERT_EQ((*round_func)->arguments.size(), 2);
    auto* decimals = std::get_if<Literal>(&(*round_func)->arguments[1]);
    ASSERT_NE(decimals, nullptr);
    auto* decimal_value = std::get_if<int64_t>(&decimals->value);
    ASSERT_NE(decimal_value, nullptr);
    EXPECT_EQ(*decimal_value, 0);

    Expr rounded_two = round_val(amount, 2);
    auto* round_two_func = std::get_if<std::shared_ptr<FunctionExpr>>(&rounded_two.node);
    ASSERT_NE(round_two_func, nullptr);
    auto* decimals_two = std::get_if<Literal>(&(*round_two_func)->arguments[1]);
    ASSERT_NE(decimals_two, nullptr);
    auto* decimal_two_value = std::get_if<int64_t>(&decimals_two->value);
    ASSERT_NE(decimal_two_value, nullptr);
    EXPECT_EQ(*decimal_two_value, 2);
}
