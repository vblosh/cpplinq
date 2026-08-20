#include <gtest/gtest.h>
#include "cpplinq/core/sql_generator.h"
#include "cpplinq/core/expression.h"
#include "cpplinq/dialect/dialect.h"
#include "dialect/informix_dialect.h"

using namespace cpplinq;

TEST(InformixQueryBuilderTest, SelectWithQuotesAndPrefixPagination) {
    InformixDialect dialect;
    SqlGenerator gen(dialect);

    ColumnHandle age("users", "age");
    ColumnHandle name("name");
    auto where_expr = (age > 21) && (name == "Alice");

    std::vector<std::pair<ExprNode, SortDir>> order_by = {
        {age.ref, SortDir::Desc},
        {name.ref, SortDir::Asc}
    };

    auto result = gen.generate_select("users", {"id", "name", "age"}, where_expr.node, order_by, 10, 20);
    EXPECT_EQ(result.sql, "SELECT SKIP 20 FIRST 10 \"id\", \"name\", \"age\" FROM \"users\" WHERE ((\"users\".\"age\" > ?) AND (\"name\" = ?)) ORDER BY \"users\".\"age\" DESC, \"name\" ASC");
    ASSERT_EQ(result.params.size(), 2);
    EXPECT_EQ(std::get<int64_t>(result.params[0]), 21);
    EXPECT_EQ(std::get<std::string>(result.params[1]), "Alice");
}

TEST(InformixQueryBuilderTest, UpsertMergeStatement) {
    InformixDialect dialect;
    SqlGenerator gen(dialect);

    std::vector<std::string> insert_cols = {"id", "name", "email", "age"};
    std::vector<BoundValue> values = {int64_t(1), std::string("Alice"), std::string("alice@test.com"), int64_t(30)};
    std::vector<std::string> conflict_cols = {"id"};
    std::vector<std::string> update_cols = {"name", "email", "age"};

    auto result = gen.generate_upsert("users", insert_cols, values, conflict_cols, update_cols);
    EXPECT_EQ(result.sql, "MERGE INTO \"users\" AS target USING (SELECT CAST(? AS BIGINT) AS \"id\", CAST(? AS VARCHAR(255)) AS \"name\", CAST(? AS VARCHAR(255)) AS \"email\", CAST(? AS BIGINT) AS \"age\" FROM \"informix\".systables WHERE tabid = 1) AS source ON (target.\"id\" = source.\"id\") WHEN MATCHED THEN UPDATE SET target.\"name\" = source.\"name\", target.\"email\" = source.\"email\", target.\"age\" = source.\"age\" WHEN NOT MATCHED THEN INSERT (\"id\", \"name\", \"email\", \"age\") VALUES (source.\"id\", source.\"name\", source.\"email\", source.\"age\")");
    EXPECT_EQ(result.params.size(), 4);
}

TEST(InformixQueryBuilderTest, DateFunctions) {
    InformixDialect dialect;
    SqlGenerator gen(dialect);

    ColumnHandle created_at("events", "created_at");

    auto year_expr = created_at.year();
    auto year_res = gen.generate_expression(year_expr.node);
    EXPECT_EQ(year_res.sql, "YEAR(CAST(\"events\".\"created_at\" AS DATETIME YEAR TO SECOND))");

    auto month_expr = created_at.month();
    auto month_res = gen.generate_expression(month_expr.node);
    EXPECT_EQ(month_res.sql, "MONTH(CAST(\"events\".\"created_at\" AS DATETIME YEAR TO SECOND))");

    auto day_expr = created_at.day();
    auto day_res = gen.generate_expression(day_expr.node);
    EXPECT_EQ(day_res.sql, "DAY(CAST(\"events\".\"created_at\" AS DATETIME YEAR TO SECOND))");

    auto add_days_expr = created_at.add_days(7);
    auto add_days_res = gen.generate_expression(add_days_expr.node);
    EXPECT_EQ(add_days_res.sql, "(\"events\".\"created_at\" + (?) UNITS DAY)");
    ASSERT_EQ(add_days_res.params.size(), 1);
    EXPECT_EQ(std::get<int64_t>(add_days_res.params[0]), 7);

    auto now_res = gen.generate_expression(current_timestamp_val().node);
    EXPECT_EQ(now_res.sql, "CURRENT YEAR TO FRACTION(3)");

    auto today_res = gen.generate_expression(current_date_val().node);
    EXPECT_EQ(today_res.sql, "TODAY");
}

TEST(InformixQueryBuilderTest, CreateTable) {
    InformixDialect dialect;
    SqlGenerator gen(dialect);

    std::vector<ColumnInfo> cols = {
        {"id", SqlType::Integer, true, true, false, false, false},
        {"name", SqlType::Text, false, false, true, false, false},
        {"email", SqlType::Text, false, false, false, false, true},
        {"active", SqlType::Boolean, false, false, true, false, false},
        {"recorded_at", SqlType::Timestamp, false, false, false, false, false}
    };

    auto result = gen.generate_create_table("users", cols);
    EXPECT_EQ(result.sql, "CREATE TABLE IF NOT EXISTS \"users\" (\"id\" SERIAL PRIMARY KEY, \"name\" VARCHAR(255) NOT NULL, \"email\" VARCHAR(255) UNIQUE, \"active\" BOOLEAN NOT NULL, \"recorded_at\" DATETIME YEAR TO FRACTION(3))");
    EXPECT_TRUE(result.params.empty());
}
