#include <gtest/gtest.h>
#include "cpplinq/core/sql_generator.h"
#include "cpplinq/core/expression.h"
#include "cpplinq/dialect/dialect.h"
#include "dialect/mssql_dialect.h"
#ifdef CPPLINQ_HAS_MSSQL
#include "driver/mssql_connection.h"
#endif
#include <stdexcept>

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

#ifdef CPPLINQ_HAS_MSSQL
TEST(MssqlConnectionTest, FactoryDialectCapabilitiesAndInfoWithoutOpening) {
    auto conn = make_connection<mssql>("MSSQLLocalDB");
    ASSERT_NE(conn, nullptr);
    EXPECT_FALSE(conn->is_open());

    const ISqlDialect& dialect = conn->dialect();
    EXPECT_EQ(dialect.quote_id("user]name"), "[user]]name]");
    EXPECT_EQ(dialect.placeholder(0), "?");
    EXPECT_EQ(dialect.placeholder(42), "?");
    EXPECT_EQ(dialect.limit_offset(10, std::nullopt),
              " OFFSET 0 ROWS FETCH NEXT 10 ROWS ONLY");
    EXPECT_EQ(dialect.limit_offset(10, 20),
              " OFFSET 20 ROWS FETCH NEXT 10 ROWS ONLY");

    const auto caps = conn->capabilities();
    EXPECT_TRUE(caps.cancel);
    EXPECT_TRUE(caps.streaming);
    EXPECT_TRUE(caps.query_timeout);
    EXPECT_TRUE(caps.transactions);
    // Documented gap: savepoints are reported as available, but there is no
    // savepoint API on IConnection or Transaction.
    EXPECT_TRUE(caps.savepoints);
    EXPECT_FALSE(caps.returning_clause);
    EXPECT_TRUE(caps.output_clause);
    EXPECT_TRUE(caps.upsert);
    EXPECT_TRUE(caps.array_batch_insert);
    EXPECT_EQ(caps.default_batch_chunk_size, 1000u);
    EXPECT_TRUE(caps.window_functions);
    EXPECT_TRUE(caps.ctes);

    const auto info = conn->info();
    EXPECT_EQ(info.driver_name, "ODBC Driver for SQL Server");
    EXPECT_EQ(info.dbms_name, "Microsoft SQL Server");
    EXPECT_TRUE(info.driver_version.empty());
    EXPECT_TRUE(info.dbms_version.empty());
    EXPECT_TRUE(info.odbc_version.empty());
}

TEST(MssqlConnectionTest, SqlserverAliasUsesMssqlFactory) {
    auto conn = make_connection<sqlserver>("Driver={ODBC Driver 18 for SQL Server};Server=localhost;Database=testdb;");
    ASSERT_NE(conn, nullptr);
    EXPECT_FALSE(conn->is_open());
    EXPECT_EQ(conn->dialect().quote_id("id"), "[id]");
    EXPECT_EQ(conn->info().dbms_name, "Microsoft SQL Server");
}
#endif

TEST(MssqlQueryBuilderTest, LimitZeroThrowsInvalidArgument) {
    MssqlDialect dialect;
    EXPECT_THROW(dialect.limit_offset(0, std::nullopt), std::invalid_argument);
    EXPECT_THROW(dialect.limit_offset(0, 0), std::invalid_argument);
    EXPECT_THROW(dialect.limit_offset(0, 20), std::invalid_argument);

    // An offset without a limit does not emit FETCH NEXT and remains valid.
    EXPECT_EQ(dialect.limit_offset(std::nullopt, 0), " OFFSET 0 ROWS");
    EXPECT_EQ(dialect.limit_offset(1, std::nullopt),
              " OFFSET 0 ROWS FETCH NEXT 1 ROWS ONLY");

    SqlGenerator gen(dialect);
    const std::vector<std::pair<ExprNode, SortDir>> no_order;
    EXPECT_THROW(gen.generate_select("users", {"id"}, std::nullopt, no_order, 0, std::nullopt),
                 std::invalid_argument);
    EXPECT_THROW(gen.generate_select("users", {"id"}, std::nullopt, no_order, 0, 10),
                 std::invalid_argument);
}

TEST(MssqlQueryBuilderTest, DocumentedReturningGapUsesOutputClause) {
    MssqlDialect dialect;
    SqlGenerator gen(dialect);

    // SQL Server has no RETURNING clause; the dialect emits OUTPUT INSERTED.
    EXPECT_EQ(dialect.returning_clause("id"), "");
    EXPECT_EQ(dialect.output_clause("id"), " OUTPUT INSERTED.[id]");

    auto result = gen.generate_insert("users", {"name"}, {std::string("Alice")}, "id");
    EXPECT_EQ(result.sql, "INSERT INTO [users] ([name]) OUTPUT INSERTED.[id] VALUES (?)");
    EXPECT_EQ(result.sql.find("RETURNING"), std::string::npos);
    EXPECT_EQ(result.sql.find("LAST_INSERT_ID"), std::string::npos);
    ASSERT_EQ(result.params.size(), 1u);
    EXPECT_EQ(std::get<std::string>(result.params[0]), "Alice");
}

TEST(MssqlQueryBuilderTest, DocumentedPaginationAndDdlGaps) {
    MssqlDialect dialect;
    SqlGenerator gen(dialect);

    // SQL Server has no LIMIT; LIMIT/OFFSET become OFFSET/FETCH NEXT, and the
    // generator supplies ORDER BY (SELECT NULL) when no ordering is requested.
    const std::vector<std::pair<ExprNode, SortDir>> no_order;
    auto limit_only = gen.generate_select("users", {"id"}, std::nullopt, no_order, 5, std::nullopt);
    EXPECT_EQ(limit_only.sql,
              "SELECT [id] FROM [users] ORDER BY (SELECT NULL) OFFSET 0 ROWS FETCH NEXT 5 ROWS ONLY");

    auto page = gen.generate_select("users", {"id"}, std::nullopt, no_order, 5, 10);
    EXPECT_EQ(page.sql,
              "SELECT [id] FROM [users] ORDER BY (SELECT NULL) OFFSET 10 ROWS FETCH NEXT 5 ROWS ONLY");

    // SQL Server has no CREATE TABLE IF NOT EXISTS; the dialect guards with
    // OBJECT_ID and creates the bracketed table name.
    EXPECT_EQ(dialect.create_table_prefix("user'table"),
              "IF OBJECT_ID(N'user''table', N'U') IS NULL CREATE TABLE [user'table]");

    // Scalar-function spellings differ from the portable names.
    EXPECT_EQ(dialect.function_name("LENGTH"), "LEN");
    EXPECT_EQ(dialect.function_name("SUBSTR"), "SUBSTRING");
    EXPECT_EQ(dialect.function_name("UPPER"), "UPPER");
}

TEST(MssqlQueryBuilderTest, IdentityTypesAndCurrentDateTime) {
    MssqlDialect dialect;
    SqlGenerator gen(dialect);

    EXPECT_EQ(dialect.auto_increment_type(), "INT IDENTITY(1,1) PRIMARY KEY");
    EXPECT_EQ(dialect.type_name(SqlType::Integer), "INT");
    EXPECT_EQ(dialect.type_name(SqlType::UnsignedBigInt), "NUMERIC(20, 0)");
    EXPECT_EQ(dialect.type_name(SqlType::Text), "NVARCHAR(255)");
    EXPECT_EQ(dialect.type_name(SqlType::Blob), "VARBINARY(MAX)");
    EXPECT_EQ(dialect.type_name(SqlType::Boolean), "BIT");
    EXPECT_EQ(dialect.type_name(SqlType::Timestamp), "DATETIME2");
    EXPECT_EQ(dialect.type_name(SqlType::Guid), "UNIQUEIDENTIFIER");

    EXPECT_EQ(gen.generate_expression(current_timestamp_val().node).sql,
              "CURRENT_TIMESTAMP");
    EXPECT_EQ(gen.generate_expression(current_date_val().node).sql,
              "CAST(GETDATE() AS DATE)");
}

TEST(MssqlQueryBuilderTest, DocumentedUpsertRequiresConflictColumns) {
    MssqlDialect dialect;

    // The documented MERGE gap: SQL Server cannot derive a conflict target
    // from an empty list, so the dialect rejects it instead of emitting SQL.
    EXPECT_THROW(dialect.generate_upsert("users", {"id", "name"}, {}, {"name"}),
                 std::invalid_argument);
    EXPECT_THROW(dialect.generate_upsert("users", {}, {"id"}, {"name"}),
                 std::invalid_argument);

    auto sql = dialect.generate_upsert("users", {"id", "name"}, {"id"}, {});
    EXPECT_EQ(sql,
              "MERGE INTO [users] WITH (HOLDLOCK) AS [target] USING (VALUES (?, ?)) AS [source] ([id], [name]) ON ([target].[id] = [source].[id]) WHEN NOT MATCHED THEN INSERT ([id], [name]) VALUES ([source].[id], [source].[name]);");
    EXPECT_EQ(sql.find("WHEN MATCHED"), std::string::npos);
}
