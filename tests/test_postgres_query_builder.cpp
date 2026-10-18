#include <gtest/gtest.h>
#include "cpplinq/core/sql_generator.h"
#include "cpplinq/core/expression.h"
#include "cpplinq/dialect/dialect.h"
#include "dialect/postgres_dialect.h"

#ifdef CPPLINQ_HAS_POSTGRES
#include "driver/postgres_connection.h"
#endif

using namespace cpplinq;

TEST(PostgresQueryBuilderTest, SelectWithQuotesAndPagination) {
    PostgresDialect dialect;
    SqlGenerator gen(dialect);

    ColumnHandle age("users", "age");
    ColumnHandle name("name");
    auto where_expr = (age > 21) && (name == "Alice");

    std::vector<std::pair<ExprNode, SortDir>> order_by = {
        {age.ref, SortDir::Desc},
        {name.ref, SortDir::Asc}
    };

    auto result = gen.generate_select("users", {"id", "name", "age"}, where_expr.node, order_by, 10, 20);
    EXPECT_EQ(result.sql, "SELECT \"id\", \"name\", \"age\" FROM \"users\" WHERE ((\"users\".\"age\" > ?) AND (\"name\" = ?)) ORDER BY \"users\".\"age\" DESC, \"name\" ASC LIMIT 10 OFFSET 20");
    ASSERT_EQ(result.params.size(), 2);
    EXPECT_EQ(std::get<int64_t>(result.params[0]), 21);
    EXPECT_EQ(std::get<std::string>(result.params[1]), "Alice");
}

TEST(PostgresQueryBuilderTest, UpsertOnConflict) {
    PostgresDialect dialect;
    SqlGenerator gen(dialect);

    std::vector<std::string> insert_cols = {"id", "name", "email", "age"};
    std::vector<BoundValue> values = {int64_t(1), std::string("Alice"), std::string("alice@test.com"), int64_t(30)};
    std::vector<std::string> conflict_cols = {"id"};
    std::vector<std::string> update_cols = {"name", "email", "age"};

    auto result = gen.generate_upsert("users", insert_cols, values, conflict_cols, update_cols);
    EXPECT_EQ(result.sql, "INSERT INTO \"users\" (\"id\", \"name\", \"email\", \"age\") VALUES (?, ?, ?, ?) ON CONFLICT (\"id\") DO UPDATE SET \"name\" = EXCLUDED.\"name\", \"email\" = EXCLUDED.\"email\", \"age\" = EXCLUDED.\"age\"");
    EXPECT_EQ(result.params.size(), 4);
}

TEST(PostgresQueryBuilderTest, ReturningClauseAndDateFunctions) {
    PostgresDialect dialect;
    SqlGenerator gen(dialect);

    ColumnHandle created_at("events", "created_at");

    auto year_expr = created_at.year();
    auto year_res = gen.generate_expression(year_expr.node);
    EXPECT_EQ(year_res.sql, "EXTRACT(YEAR FROM CAST(\"events\".\"created_at\" AS TIMESTAMP))");

    auto month_expr = created_at.month();
    auto month_res = gen.generate_expression(month_expr.node);
    EXPECT_EQ(month_res.sql, "EXTRACT(MONTH FROM CAST(\"events\".\"created_at\" AS TIMESTAMP))");

    auto day_expr = created_at.day();
    auto day_res = gen.generate_expression(day_expr.node);
    EXPECT_EQ(day_res.sql, "EXTRACT(DAY FROM CAST(\"events\".\"created_at\" AS TIMESTAMP))");

    auto add_expr = created_at.add_days(7);
    auto add_res = gen.generate_expression(add_expr.node);
    EXPECT_EQ(add_res.sql, "(CAST(\"events\".\"created_at\" AS TIMESTAMP) + (? || ' days')::interval)");

    EXPECT_EQ(dialect.returning_clause("id"), " RETURNING \"id\"");
    EXPECT_EQ(dialect.auto_increment_type(), "BIGSERIAL PRIMARY KEY");
    EXPECT_EQ(dialect.type_name(SqlType::Integer), "INTEGER");
    EXPECT_EQ(dialect.type_name(SqlType::BigInt), "BIGINT");
    EXPECT_EQ(dialect.type_name(SqlType::Text), "TEXT");
    EXPECT_EQ(dialect.type_name(SqlType::Blob), "BYTEA");
    EXPECT_EQ(dialect.type_name(SqlType::Boolean), "BOOLEAN");
}

TEST(PostgresQueryBuilderTest, DialectPlaceholdersStayQuestionMarks) {
    PostgresDialect dialect;

    EXPECT_EQ(dialect.placeholder(0), "?");
    EXPECT_EQ(dialect.placeholder(1), "?");
    EXPECT_EQ(dialect.placeholder(99), "?");

    EXPECT_EQ(dialect.limit_offset(10, std::nullopt), " LIMIT 10");
    EXPECT_EQ(dialect.limit_offset(std::nullopt, 20), " OFFSET 20");
    EXPECT_EQ(dialect.limit_offset(10, 20), " LIMIT 10 OFFSET 20");
    EXPECT_EQ(dialect.limit_offset(std::nullopt, std::nullopt), "");
}

TEST(PostgresQueryBuilderTest, GeneratedSqlKeepsQuestionMarksForDriverRewrite) {
    PostgresDialect dialect;
    SqlGenerator gen(dialect);
    ColumnHandle id("users", "id");

    auto select = gen.generate_select("users", {"id"}, (id > 7).node);
    EXPECT_EQ(select.sql, "SELECT \"id\" FROM \"users\" WHERE (\"users\".\"id\" > ?)");
    EXPECT_EQ(select.sql.find('$'), std::string::npos);
    ASSERT_EQ(select.params.size(), 1u);
    EXPECT_EQ(std::get<int64_t>(select.params[0]), 7);

    auto insert = gen.generate_insert(
        "users", {"id", "name"}, {int64_t(1), std::string("Alice")}, "id");
    EXPECT_EQ(insert.sql,
              "INSERT INTO \"users\" (\"id\", \"name\") VALUES (?, ?) RETURNING \"id\"");
    EXPECT_EQ(insert.sql.find('$'), std::string::npos);
    EXPECT_EQ(insert.sql.find("LAST_INSERT_ID"), std::string::npos);

    std::vector<std::pair<std::string, BoundValue>> assignments;
    assignments.emplace_back("name", std::string("Alice"));
    auto update = gen.generate_update("users", assignments, std::nullopt);
    EXPECT_EQ(update.sql, "UPDATE \"users\" SET \"name\" = ?");
    EXPECT_EQ(update.sql.find('$'), std::string::npos);

    auto remove = gen.generate_delete("users", (id == int64_t(3)).node);
    EXPECT_EQ(remove.sql, "DELETE FROM \"users\" WHERE (\"users\".\"id\" = ?)");
    EXPECT_EQ(remove.sql.find('$'), std::string::npos);
}

TEST(PostgresQueryBuilderTest, DialectTypeNamesCoverAllMappedTypes) {
    PostgresDialect dialect;

    EXPECT_EQ(dialect.type_name(SqlType::UnsignedBigInt), "NUMERIC(20, 0)");
    EXPECT_EQ(dialect.type_name(SqlType::Real), "DOUBLE PRECISION");
    EXPECT_EQ(dialect.type_name(SqlType::Decimal), "NUMERIC");
    EXPECT_EQ(dialect.type_name(SqlType::WString), "TEXT");
    EXPECT_EQ(dialect.type_name(SqlType::Date), "DATE");
    EXPECT_EQ(dialect.type_name(SqlType::Time), "TIME");
    EXPECT_EQ(dialect.type_name(SqlType::Timestamp), "TIMESTAMP");
    EXPECT_EQ(dialect.type_name(SqlType::Interval), "INTERVAL");
    EXPECT_EQ(dialect.type_name(SqlType::Guid), "UUID");
}

TEST(PostgresQueryBuilderTest, CurrentDateAndTimestampExpressions) {
    PostgresDialect dialect;
    SqlGenerator gen(dialect);

    EXPECT_EQ(gen.generate_expression(current_timestamp_val().node).sql,
              "CURRENT_TIMESTAMP");
    EXPECT_EQ(gen.generate_expression(current_date_val().node).sql,
              "CURRENT_DATE");
}

#ifdef CPPLINQ_HAS_POSTGRES

TEST(PostgresQueryBuilderTest, FactoryAndIntrospectionWorkWhileClosed) {
    auto conn = make_connection<postgres>(
        "host=127.0.0.1 port=1 dbname=cppdb user=cppdb password=cppdb sslmode=disable");
    ASSERT_NE(conn, nullptr);
    EXPECT_FALSE(conn->is_open());

    const ISqlDialect& dialect = conn->dialect();
    EXPECT_EQ(dialect.placeholder(0), "?");
    EXPECT_EQ(dialect.quote_id("users"), "\"users\"");
    EXPECT_EQ(dialect.returning_clause("id"), " RETURNING \"id\"");

    DriverInfo info = conn->info();
    EXPECT_EQ(info.driver_name, "PostgreSQL Native Driver (libpq)");
    EXPECT_FALSE(info.driver_version.empty());
    EXPECT_EQ(info.dbms_name, "PostgreSQL");
    EXPECT_EQ(info.dbms_version, "unknown");
    EXPECT_EQ(info.odbc_version, "N/A");

    DriverCapabilities caps = conn->capabilities();
    EXPECT_TRUE(caps.cancel);
    EXPECT_TRUE(caps.streaming);
    EXPECT_TRUE(caps.query_timeout);
    EXPECT_TRUE(caps.transactions);
    EXPECT_TRUE(caps.savepoints);
    EXPECT_TRUE(caps.returning_clause);
    EXPECT_FALSE(caps.output_clause);
    EXPECT_TRUE(caps.upsert);
    EXPECT_TRUE(caps.array_batch_insert);
    EXPECT_EQ(caps.default_batch_chunk_size, size_t(50000));
    EXPECT_TRUE(caps.window_functions);
    EXPECT_TRUE(caps.ctes);
}

TEST(PostgresQueryBuilderTest, BareDsnFactoryDoesNotOpenOrResolve) {
    auto conn = make_connection<postgres>("PostgreSQL35W");
    ASSERT_NE(conn, nullptr);
    EXPECT_FALSE(conn->is_open());
    EXPECT_EQ(conn->info().dbms_version, "unknown");
}

TEST(PostgresQueryBuilderTest, NativePreparedRewriteRequiresOpenConnection) {
    PgConnection conn(
        "host=127.0.0.1 port=1 dbname=cppdb user=cppdb password=cppdb sslmode=disable");
    EXPECT_FALSE(conn.is_open());

    // The '?' -> '$n' conversion happens in PgPreparedStatement's constructor.
    // PgConnection::prepare() refuses to create that statement while closed, and
    // the converted SQL is private, so no public path exposes it without a server.
    EXPECT_THROW(conn.prepare("SELECT \"id\" FROM \"users\" WHERE \"id\" = ?"), DbException);

    PgPreparedStatement query(nullptr, "SELECT \"id\" FROM \"users\" WHERE \"id\" = ?");
    EXPECT_NO_THROW(query.bind(0, int64_t(7)));
    EXPECT_THROW(query.execute_query(), DbException);

    PgPreparedStatement command(
        nullptr, "UPDATE \"users\" SET \"name\" = ? WHERE \"id\" = ?");
    EXPECT_NO_THROW(command.bind(0, std::string("Alice")));
    EXPECT_NO_THROW(command.bind(1, int64_t(7)));
    EXPECT_THROW(command.execute_non_query(), DbException);
}

TEST(PostgresQueryBuilderTest, DirectExecutionPathsAreSeparateFromPreparedRewrite) {
    PgConnection conn("");
    EXPECT_FALSE(conn.is_open());

    // Direct execution sends SQL verbatim according to the driver documentation;
    // it does not go through the native prepared-statement '?' rewrite.
    EXPECT_THROW(conn.execute("SELECT ?"), DbException);
    EXPECT_THROW(conn.execute_query_direct("SELECT ?"), DbException);
    EXPECT_THROW(conn.execute_non_query_direct("UPDATE \"users\" SET \"name\" = ?"),
                 DbException);
}

TEST(PostgresQueryBuilderTest, InheritedLastInsertIdFallbackIsNotPostgresSql) {
    PostgresDialect dialect;

    // PostgreSQL has no LAST_INSERT_ID(); inserts that need the generated key use
    // RETURNING. This inherited MySQL fallback remains exposed by the dialect API.
    EXPECT_TRUE(dialect.supports_last_insert_id());
    EXPECT_EQ(dialect.last_insert_id_query("users", "id"), "SELECT LAST_INSERT_ID()");
}

#endif // CPPLINQ_HAS_POSTGRES
