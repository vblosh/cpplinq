#include <gtest/gtest.h>
#include "cpplinq/core/sql_generator.h"
#include "cpplinq/core/expression.h"
#include "cpplinq/dialect/dialect.h"
#include "dialect/mssql_dialect.h"

using namespace cpplinq;

TEST(MssqlQueryBuilderTest, SelectWithBracketsAndPagination) {
    MssqlDialect dialect;
    SqlGenerator gen(dialect);

    ColumnHandle age("users", "age");
    ColumnHandle name("name");
    auto where_expr = (age > 21) && (name == "Alice");

    std::vector<std::pair<ExprNode, SortDir>> order_by = {
        {age.ref, SortDir::Desc},
        {name.ref, SortDir::Asc}
    };

    auto result = gen.generate_select("users", {"id", "name", "age"}, where_expr.node, order_by, 10, 20);
    EXPECT_EQ(result.sql, "SELECT [id], [name], [age] FROM [users] WHERE (([users].[age] > ?) AND ([name] = ?)) ORDER BY [users].[age] DESC, [name] ASC OFFSET 20 ROWS FETCH NEXT 10 ROWS ONLY");
    ASSERT_EQ(result.params.size(), 2);
    EXPECT_EQ(std::get<int64_t>(result.params[0]), 21);
    EXPECT_EQ(std::get<std::string>(result.params[1]), "Alice");
}

TEST(MssqlQueryBuilderTest, UpsertMergeStatement) {
    MssqlDialect dialect;
    SqlGenerator gen(dialect);

    std::vector<std::string> insert_cols = {"id", "name", "email", "age"};
    std::vector<BoundValue> values = {int64_t(1), std::string("Alice"), std::string("alice@test.com"), int64_t(30)};
    std::vector<std::string> conflict_cols = {"id"};
    std::vector<std::string> update_cols = {"name", "email", "age"};

    auto result = gen.generate_upsert("users", insert_cols, values, conflict_cols, update_cols);
    EXPECT_EQ(result.sql, "MERGE INTO [users] WITH (HOLDLOCK) AS [target] USING (VALUES (?, ?, ?, ?)) AS [source] ([id], [name], [email], [age]) ON ([target].[id] = [source].[id]) WHEN MATCHED THEN UPDATE SET [target].[name] = [source].[name], [target].[email] = [source].[email], [target].[age] = [source].[age] WHEN NOT MATCHED THEN INSERT ([id], [name], [email], [age]) VALUES ([source].[id], [source].[name], [source].[email], [source].[age]);");
    EXPECT_EQ(result.params.size(), 4);
}

TEST(MssqlQueryBuilderTest, DateFunctions) {
    MssqlDialect dialect;
    SqlGenerator gen(dialect);

    ColumnHandle created_at("events", "created_at");

    auto year_expr = created_at.year();
    auto year_res = gen.generate_expression(year_expr.node);
    EXPECT_EQ(year_res.sql, "YEAR([events].[created_at])");

    auto month_expr = created_at.month();
    auto month_res = gen.generate_expression(month_expr.node);
    EXPECT_EQ(month_res.sql, "MONTH([events].[created_at])");

    auto day_expr = created_at.day();
    auto day_res = gen.generate_expression(day_expr.node);
    EXPECT_EQ(day_res.sql, "DAY([events].[created_at])");

    auto add_expr = created_at.add_days(7);
    auto add_res = gen.generate_expression(add_expr.node);
    EXPECT_EQ(add_res.sql, "DATEADD(day, ?, [events].[created_at])");
}
