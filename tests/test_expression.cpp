#include <gtest/gtest.h>
#include "cpplinq/core/expression.h"
#include <iostream>

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
