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
