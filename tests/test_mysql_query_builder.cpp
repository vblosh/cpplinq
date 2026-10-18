#include <gtest/gtest.h>
#include "cpplinq/core/sql_generator.h"
#include "cpplinq/core/expression.h"
#include "cpplinq/dialect/dialect.h"
#include "dialect/mysql_dialect.h"

using namespace cpplinq;

TEST(MysqlQueryBuilderTest, SelectWithBackticksAndPagination) {
    MysqlDialect dialect;
    SqlGenerator gen(dialect);

    ColumnHandle age("users", "age");
    ColumnHandle name("name");
    auto where_expr = (age > 21) && (name == "Alice");

    std::vector<std::pair<ExprNode, SortDir>> order_by = {
        {age.ref, SortDir::Desc},
        {name.ref, SortDir::Asc}
    };

    auto result = gen.generate_select("users", {"id", "name", "age"}, where_expr.node, order_by, 10, 20);
    EXPECT_EQ(result.sql, "SELECT `id`, `name`, `age` FROM `users` WHERE ((`users`.`age` > ?) AND (`name` = ?)) ORDER BY `users`.`age` DESC, `name` ASC LIMIT 10 OFFSET 20");
    ASSERT_EQ(result.params.size(), 2);
    EXPECT_EQ(std::get<int64_t>(result.params[0]), 21);
    EXPECT_EQ(std::get<std::string>(result.params[1]), "Alice");
}

TEST(MysqlQueryBuilderTest, UpsertOnDuplicateKey) {
    MysqlDialect dialect;
    SqlGenerator gen(dialect);

    std::vector<std::string> insert_cols = {"id", "name", "email", "age"};
    std::vector<BoundValue> values = {int64_t(1), std::string("Alice"), std::string("alice@test.com"), int64_t(30)};
    std::vector<std::string> conflict_cols = {"id"};
    std::vector<std::string> update_cols = {"name", "email", "age"};

    auto result = gen.generate_upsert("users", insert_cols, values, conflict_cols, update_cols);
    EXPECT_EQ(result.sql, "INSERT INTO `users` (`id`, `name`, `email`, `age`) VALUES (?, ?, ?, ?) ON DUPLICATE KEY UPDATE `name` = VALUES(`name`), `email` = VALUES(`email`), `age` = VALUES(`age`)");
    EXPECT_EQ(result.params.size(), 4);
}

TEST(MysqlQueryBuilderTest, DateFunctions) {
    MysqlDialect dialect;
    SqlGenerator gen(dialect);

    ColumnHandle created_at("events", "created_at");

    auto year_expr = created_at.year();
    auto year_res = gen.generate_expression(year_expr.node);
    EXPECT_EQ(year_res.sql, "YEAR(`events`.`created_at`)");

    auto month_expr = created_at.month();
    auto month_res = gen.generate_expression(month_expr.node);
    EXPECT_EQ(month_res.sql, "MONTH(`events`.`created_at`)");

    auto day_expr = created_at.day();
    auto day_res = gen.generate_expression(day_expr.node);
    EXPECT_EQ(day_res.sql, "DAY(`events`.`created_at`)");

    auto add_expr = created_at.add_days(7);
    auto add_res = gen.generate_expression(add_expr.node);
    EXPECT_EQ(add_res.sql, "DATE_ADD(`events`.`created_at`, INTERVAL (?) DAY)");
}

#ifdef CPPLINQ_HAS_MYSQL
#include "driver/mysql_connection.h"
#include "dialect/mssql_dialect.h"
#include "dialect/postgres_dialect.h"

TEST(MysqlConnectionTest, FactoryMetadataWithoutOpening) {
    auto conn = make_connection<mysql>(
        "host=db.example;port=3307;database=appdb;user=alice;password=s3cret");
    ASSERT_NE(conn, nullptr);
    EXPECT_FALSE(conn->is_open());

    const ISqlDialect& dialect = conn->dialect();
    EXPECT_EQ(dialect.quote_id("users"), "`users`");
    EXPECT_EQ(dialect.quote_id("odd`name"), "`odd``name`");
    EXPECT_EQ(dialect.placeholder(0), "?");
    EXPECT_EQ(dialect.placeholder(42), "?");
    EXPECT_EQ(dialect.type_name(SqlType::Integer), "INT");
    EXPECT_EQ(dialect.type_name(SqlType::UnsignedBigInt), "BIGINT UNSIGNED");
    EXPECT_EQ(dialect.type_name(SqlType::WString), "VARCHAR(255) CHARACTER SET utf8mb4");
    EXPECT_EQ(dialect.auto_increment_type(), "INT AUTO_INCREMENT PRIMARY KEY");

    const DriverCapabilities caps = conn->capabilities();
    EXPECT_TRUE(caps.cancel);
    EXPECT_TRUE(caps.streaming);
    EXPECT_TRUE(caps.query_timeout);
    EXPECT_TRUE(caps.transactions);
    EXPECT_TRUE(caps.savepoints);
    EXPECT_FALSE(caps.returning_clause);
    EXPECT_FALSE(caps.output_clause);
    EXPECT_TRUE(caps.upsert);
    EXPECT_TRUE(caps.array_batch_insert);
    EXPECT_EQ(caps.default_batch_chunk_size, 5000u);
    EXPECT_TRUE(caps.window_functions);
    EXPECT_TRUE(caps.ctes);

    const DriverInfo info = conn->info();
    EXPECT_EQ(info.driver_name, "MySQL Native Driver (libmariadb / libmysqlclient)");
    EXPECT_EQ(info.driver_version, std::to_string(mysql_get_client_version()));
    EXPECT_EQ(info.dbms_name, "MySQL");
    EXPECT_EQ(info.dbms_version, "unknown");
    EXPECT_EQ(info.odbc_version, "N/A");
    EXPECT_FALSE(conn->is_open());
}

TEST(MysqlConnectionTest, MariaDbAliasUsesTheSameFactory) {
    auto conn = make_connection<mariadb>("mysql://alice:s3cret@db.example:3307/appdb");
    ASSERT_NE(conn, nullptr);
    EXPECT_FALSE(conn->is_open());
    EXPECT_EQ(conn->dialect().quote_id("column"), "`column`");
    EXPECT_EQ(conn->info().dbms_name, "MySQL");
}

TEST(MysqlDialectTest, CapabilitiesDifferFromOtherDialects) {
    MysqlDialect mysql_dialect;
    PostgresDialect postgres_dialect;
    MssqlDialect mssql_dialect;

    EXPECT_EQ(mysql_dialect.returning_clause("id"), "");
    EXPECT_NE(postgres_dialect.returning_clause("id"), "");
    EXPECT_EQ(mysql_dialect.output_clause("id"), "");
    EXPECT_NE(mssql_dialect.output_clause("id"), "");

    EXPECT_EQ(mysql_dialect.limit_offset(0, std::nullopt), " LIMIT 0");
    EXPECT_THROW(mssql_dialect.limit_offset(0, std::nullopt), std::invalid_argument);
    EXPECT_EQ(mysql_dialect.limit_offset(std::nullopt, 5),
              " LIMIT 18446744073709551615 OFFSET 5");
    EXPECT_EQ(postgres_dialect.limit_offset(std::nullopt, 5), " OFFSET 5");
    EXPECT_EQ(mssql_dialect.limit_offset(std::nullopt, 5), " OFFSET 5 ROWS");

    const DriverCapabilities defaults;
    EXPECT_FALSE(defaults.savepoints);
    EXPECT_FALSE(defaults.upsert);
    EXPECT_FALSE(defaults.array_batch_insert);
    EXPECT_EQ(defaults.default_batch_chunk_size, 1000u);
}

TEST(MysqlDialectTest, ReturningAndOutputAreDocumentedGaps) {
    MysqlDialect dialect;
    SqlGenerator gen(dialect);

    EXPECT_EQ(dialect.returning_clause("id"), "");
    EXPECT_EQ(dialect.output_clause("id"), "");
    EXPECT_TRUE(dialect.supports_last_insert_id());
    EXPECT_EQ(dialect.last_insert_id_query("users", "id"), "SELECT LAST_INSERT_ID()");

    const auto insert = gen.generate_insert(
        "users", {"name"}, {std::string("Alice")}, std::string_view("id"));
    EXPECT_EQ(insert.sql, "INSERT INTO `users` (`name`) VALUES (?)");
    ASSERT_EQ(insert.params.size(), 1u);
    EXPECT_EQ(std::get<std::string>(insert.params[0]), "Alice");
}

TEST(MysqlDialectTest, CurrentDateAndTimestampExpressions) {
    MysqlDialect dialect;
    SqlGenerator gen(dialect);

    EXPECT_EQ(gen.generate_expression(current_timestamp_val().node).sql,
              "CURRENT_TIMESTAMP");
    EXPECT_EQ(gen.generate_expression(current_date_val().node).sql,
              "CURRENT_DATE()");
}

TEST(MysqlDialectTest, SetOperationsEmitAnsiKeywords) {
    MysqlDialect dialect;
    SqlGenerator gen(dialect);

    SetOpClause intersect;
    intersect.op_type = SetOpType::Intersect;
    intersect.table_name = "active_users";
    intersect.columns = {"id"};

    SetOpClause except;
    except.op_type = SetOpType::Except;
    except.table_name = "archived_users";
    except.columns = {"id"};

    const auto result = gen.generate_set_operation(
        "current_users", {"id"}, std::nullopt, false, {intersect, except});
    EXPECT_EQ(result.sql,
              "SELECT `id` FROM `current_users` INTERSECT SELECT `id` FROM `active_users` "
              "EXCEPT SELECT `id` FROM `archived_users`");
}

#endif // CPPLINQ_HAS_MYSQL
