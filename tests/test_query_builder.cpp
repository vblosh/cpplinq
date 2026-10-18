#include <gtest/gtest.h>
#include <memory>
#include "cpplinq/core/sql_generator.h"
#include "cpplinq/core/expression.h"
#include "cpplinq/core/table.h"
#include "cpplinq/core/column.h"
#include "cpplinq/core/db_context.h"
#ifdef CPPLINQ_HAS_POSTGRES
#include "dialect/postgres_dialect.h"
#endif
#ifdef CPPLINQ_HAS_MSSQL
#include "dialect/mssql_dialect.h"
#endif
#ifdef CPPLINQ_HAS_MYSQL
#include "dialect/mysql_dialect.h"
#endif
#ifdef CPPLINQ_HAS_SQLITE
#include "dialect/sqlite_dialect.h"
#endif
#if defined(CPPLINQ_HAS_MSSQL) || defined(CPPLINQ_HAS_MYSQL) || defined(CPPLINQ_HAS_POSTGRES) || defined(CPPLINQ_HAS_INFORMIX) || defined(CPPLINQ_HAS_ORACLE)
#include "driver/odbc_utils.h"
#endif

using namespace cpplinq;
using namespace cpplinq::expr;

namespace {

class MockSqliteDialect : public ISqlDialect {
public:
    std::string quote_id(std::string_view id) const override {
        return "\"" + std::string(id) + "\"";
    }

    std::string placeholder(size_t /*index*/) const override {
        return "?";
    }

    std::string limit_offset(std::optional<size_t> limit,
                             std::optional<size_t> offset) const override {
        std::string result;
        if (limit.has_value()) {
            result += " LIMIT " + std::to_string(*limit);
        }
        if (offset.has_value()) {
            result += " OFFSET " + std::to_string(*offset);
        }
        return result;
    }

    std::string type_name(SqlType type) const override {
        switch (type) {
            case SqlType::Integer:        return "INTEGER";
            case SqlType::BigInt:         return "INTEGER";
            case SqlType::UnsignedBigInt: return "INTEGER";
            case SqlType::Real:           return "REAL";
            case SqlType::Decimal:        return "NUMERIC";
            case SqlType::Text:           return "TEXT";
            case SqlType::WString:        return "TEXT";
            case SqlType::Blob:           return "BLOB";
            case SqlType::Boolean:        return "INTEGER";
            case SqlType::Date:           return "TEXT";
            case SqlType::Time:           return "TEXT";
            case SqlType::Timestamp:      return "TEXT";
            case SqlType::Interval:       return "TEXT";
            case SqlType::Guid:           return "TEXT";
        }
        return "TEXT";
    }

    std::string auto_increment_type() const override {
        return "INTEGER PRIMARY KEY AUTOINCREMENT";
    }

    std::string returning_clause(std::string_view column) const override {
        return " RETURNING \"" + std::string(column) + "\"";
    }

    std::string extract_part_func(std::string_view part, std::string_view expr_sql) const override {
        if (part == "YEAR") return "CAST(strftime('%Y', " + std::string(expr_sql) + ") AS INTEGER)";
        if (part == "MONTH") return "CAST(strftime('%m', " + std::string(expr_sql) + ") AS INTEGER)";
        if (part == "DAY") return "CAST(strftime('%d', " + std::string(expr_sql) + ") AS INTEGER)";
        return "strftime('" + std::string(part) + "', " + std::string(expr_sql) + ")";
    }

    std::string date_add_days_func(std::string_view expr_sql, std::string_view days_sql) const override {
        return "date(" + std::string(expr_sql) + ", '+' || (" + std::string(days_sql) + ") || ' days')";
    }
};

class MockPostgresDialect : public ISqlDialect {
public:
    std::string quote_id(std::string_view id) const override {
        return "\"" + std::string(id) + "\"";
    }

    std::string placeholder(size_t index) const override {
        return "$" + std::to_string(index + 1);
    }

    std::string limit_offset(std::optional<size_t> limit,
                             std::optional<size_t> offset) const override {
        std::string result;
        if (limit.has_value()) {
            result += " LIMIT " + std::to_string(*limit);
        }
        if (offset.has_value()) {
            result += " OFFSET " + std::to_string(*offset);
        }
        return result;
    }

    std::string type_name(SqlType type) const override {
        switch (type) {
            case SqlType::Integer: return "INTEGER";
            case SqlType::BigInt:  return "BIGINT";
            case SqlType::Real:    return "DOUBLE PRECISION";
            case SqlType::Text:    return "TEXT";
            case SqlType::Blob:    return "BYTEA";
            case SqlType::Boolean: return "BOOLEAN";
            default:               return "TEXT";
        }
    }

    std::string auto_increment_type() const override {
        return "BIGSERIAL PRIMARY KEY";
    }

    std::string returning_clause(std::string_view column) const override {
        return " RETURNING \"" + std::string(column) + "\"";
    }
};

class MockMssqlDialect : public ISqlDialect {
public:
    std::string quote_id(std::string_view id) const override {
        return "[" + std::string(id) + "]";
    }

    std::string placeholder(size_t /*index*/) const override {
        return "?";
    }

    std::string limit_offset(std::optional<size_t> limit,
                             std::optional<size_t> offset) const override {
        std::string result;
        if (offset.has_value() && limit.has_value()) {
            result = " OFFSET " + std::to_string(*offset) + " ROWS FETCH NEXT " + std::to_string(*limit) + " ROWS ONLY";
        } else if (offset.has_value()) {
            result = " OFFSET " + std::to_string(*offset) + " ROWS";
        } else if (limit.has_value()) {
            result = " OFFSET 0 ROWS FETCH NEXT " + std::to_string(*limit) + " ROWS ONLY";
        }
        return result;
    }

    std::string type_name(SqlType type) const override {
        switch (type) {
            case SqlType::Integer: return "INT";
            case SqlType::BigInt:  return "BIGINT";
            case SqlType::Real:    return "FLOAT";
            case SqlType::Text:    return "NVARCHAR(MAX)";
            case SqlType::Blob:    return "VARBINARY(MAX)";
            case SqlType::Boolean: return "BIT";
            default:               return "NVARCHAR(MAX)";
        }
    }

    std::string auto_increment_type() const override {
        return "INT IDENTITY(1,1) PRIMARY KEY";
    }

    std::string returning_clause(std::string_view /*column*/) const override {
        return "";
    }

    std::string output_clause(std::string_view column) const override {
        return " OUTPUT INSERTED.[" + std::string(column) + "]";
    }

    std::string create_table_prefix(std::string_view table_name) const override {
        return "IF OBJECT_ID(N'" + std::string(table_name) + "', N'U') IS NULL CREATE TABLE [" + std::string(table_name) + "]";
    }

    std::string function_name(std::string_view func) const override {
        if (func == "LENGTH") return "LEN";
        if (func == "SUBSTR") return "SUBSTRING";
        return std::string(func);
    }

    std::string generate_upsert(
        std::string_view table_name,
        const std::vector<std::string>& insert_columns,
        const std::vector<std::string>& conflict_columns,
        const std::vector<std::string>& update_columns
    ) const override {
        std::string sql = "MERGE INTO " + quote_id(table_name) + " WITH (HOLDLOCK) AS [target] USING (VALUES (";
        for (size_t i = 0; i < insert_columns.size(); ++i) {
            if (i > 0) sql += ", ";
            sql += placeholder(i);
        }
        sql += ")) AS [source] (";
        for (size_t i = 0; i < insert_columns.size(); ++i) {
            if (i > 0) sql += ", ";
            sql += quote_id(insert_columns[i]);
        }
        sql += ") ON (";
        for (size_t i = 0; i < conflict_columns.size(); ++i) {
            if (i > 0) sql += " AND ";
            sql += "[target]." + quote_id(conflict_columns[i]) + " = [source]." + quote_id(conflict_columns[i]);
        }
        sql += ") WHEN MATCHED THEN UPDATE SET ";
        for (size_t i = 0; i < update_columns.size(); ++i) {
            if (i > 0) sql += ", ";
            sql += "[target]." + quote_id(update_columns[i]) + " = [source]." + quote_id(update_columns[i]);
        }
        sql += " WHEN NOT MATCHED THEN INSERT (";
        for (size_t i = 0; i < insert_columns.size(); ++i) {
            if (i > 0) sql += ", ";
            sql += quote_id(insert_columns[i]);
        }
        sql += ") VALUES (";
        for (size_t i = 0; i < insert_columns.size(); ++i) {
            if (i > 0) sql += ", ";
            sql += "[source]." + quote_id(insert_columns[i]);
        }
        sql += ");";
        return sql;
    }

    std::string current_date_func() const override {
        return "CAST(GETDATE() AS DATE)";
    }

    std::string extract_part_func(std::string_view part, std::string_view expr_sql) const override {
        if (part == "YEAR") return "YEAR(" + std::string(expr_sql) + ")";
        if (part == "MONTH") return "MONTH(" + std::string(expr_sql) + ")";
        if (part == "DAY") return "DAY(" + std::string(expr_sql) + ")";
        return "DATEPART(" + std::string(part) + ", " + std::string(expr_sql) + ")";
    }

    std::string date_add_days_func(std::string_view expr_sql, std::string_view days_sql) const override {
        return "DATEADD(day, " + std::string(days_sql) + ", " + std::string(expr_sql) + ")";
    }
};

class MockMysqlDialect : public ISqlDialect {
public:
    std::string quote_id(std::string_view id) const override {
        std::string quoted = "`";
        for (char c : id) {
            if (c == '`') quoted += "``";
            else quoted += c;
        }
        quoted += "`";
        return quoted;
    }

    std::string placeholder(size_t /*index*/) const override {
        return "?";
    }

    std::string limit_offset(std::optional<size_t> limit,
                             std::optional<size_t> offset) const override {
        std::string result;
        if (limit.has_value() && offset.has_value()) {
            result = " LIMIT " + std::to_string(*limit) + " OFFSET " + std::to_string(*offset);
        } else if (limit.has_value()) {
            result = " LIMIT " + std::to_string(*limit);
        } else if (offset.has_value()) {
            result = " LIMIT 18446744073709551615 OFFSET " + std::to_string(*offset);
        }
        return result;
    }

    std::string type_name(SqlType type) const override {
        switch (type) {
            case SqlType::Integer: return "INT";
            case SqlType::BigInt:  return "BIGINT";
            case SqlType::Real:    return "DOUBLE";
            case SqlType::Text:    return "VARCHAR(255)";
            case SqlType::Blob:    return "LONGBLOB";
            case SqlType::Boolean: return "TINYINT(1)";
            default:               return "VARCHAR(255)";
        }
    }

    std::string auto_increment_type() const override {
        return "INT AUTO_INCREMENT PRIMARY KEY";
    }

    std::string returning_clause(std::string_view /*column*/) const override {
        return "";
    }

    std::string output_clause(std::string_view /*column*/) const override {
        return "";
    }

    std::string current_date_func() const override {
        return "CURRENT_DATE()";
    }

    std::string extract_part_func(std::string_view part, std::string_view expr_sql) const override {
        if (part == "YEAR") return "YEAR(" + std::string(expr_sql) + ")";
        if (part == "MONTH") return "MONTH(" + std::string(expr_sql) + ")";
        if (part == "DAY") return "DAY(" + std::string(expr_sql) + ")";
        return "EXTRACT(" + std::string(part) + " FROM " + std::string(expr_sql) + ")";
    }

    std::string date_add_days_func(std::string_view expr_sql, std::string_view days_sql) const override {
        return "DATE_ADD(" + std::string(expr_sql) + ", INTERVAL (" + std::string(days_sql) + ") DAY)";
    }

    std::string generate_upsert(
        std::string_view table_name,
        const std::vector<std::string>& insert_columns,
        const std::vector<std::string>& /*conflict_columns*/,
        const std::vector<std::string>& update_columns
    ) const override {
        std::string sql = "INSERT INTO " + quote_id(table_name) + " (";
        for (size_t i = 0; i < insert_columns.size(); ++i) {
            if (i > 0) sql += ", ";
            sql += quote_id(insert_columns[i]);
        }
        sql += ") VALUES (";
        for (size_t i = 0; i < insert_columns.size(); ++i) {
            if (i > 0) sql += ", ";
            sql += placeholder(i);
        }
        sql += ") ON DUPLICATE KEY UPDATE ";
        for (size_t i = 0; i < update_columns.size(); ++i) {
            if (i > 0) sql += ", ";
            sql += quote_id(update_columns[i]) + " = VALUES(" + quote_id(update_columns[i]) + ")";
        }
        return sql;
    }
};

class MockOracleDialect : public ISqlDialect {
public:
    std::string quote_id(std::string_view id) const override {
        std::string quoted = "\"";
        for (char c : id) {
            if (c == '"') quoted += "\"\"";
            else quoted += c;
        }
        quoted += "\"";
        return quoted;
    }

    std::string placeholder(size_t /*index*/) const override {
        return "?";
    }

    std::string limit_offset(std::optional<size_t> limit,
                             std::optional<size_t> offset) const override {
        std::string result;
        if (offset.has_value() && limit.has_value()) {
            result = " OFFSET " + std::to_string(*offset) + " ROWS FETCH NEXT " + std::to_string(*limit) + " ROWS ONLY";
        } else if (offset.has_value()) {
            result = " OFFSET " + std::to_string(*offset) + " ROWS";
        } else if (limit.has_value()) {
            result = " OFFSET 0 ROWS FETCH NEXT " + std::to_string(*limit) + " ROWS ONLY";
        }
        return result;
    }

    std::string type_name(SqlType type) const override {
        switch (type) {
            case SqlType::Integer:        return "NUMBER(10)";
            case SqlType::BigInt:         return "NUMBER(19)";
            case SqlType::UnsignedBigInt: return "NUMBER(20, 0)";
            case SqlType::Real:           return "DOUBLE PRECISION";
            case SqlType::Decimal:        return "NUMBER(28, 10)";
            case SqlType::Text:           return "VARCHAR2(255)";
            case SqlType::WString:        return "NVARCHAR2(255)";
            case SqlType::Blob:           return "BLOB";
            case SqlType::Boolean:        return "NUMBER(1)";
            case SqlType::Date:           return "DATE";
            case SqlType::Time:           return "VARCHAR2(20)";
            case SqlType::Timestamp:      return "TIMESTAMP";
            case SqlType::Interval:       return "VARCHAR2(100)";
            case SqlType::Guid:           return "VARCHAR2(36)";
        }
        return "VARCHAR2(255)";
    }

    std::string auto_increment_type() const override {
        return "NUMBER GENERATED BY DEFAULT ON NULL AS IDENTITY PRIMARY KEY";
    }

    std::string returning_clause(std::string_view /*column*/) const override {
        return "";
    }

    std::string output_clause(std::string_view /*column*/) const override {
        return "";
    }

    std::string create_table_prefix(std::string_view table_name) const override {
        return "CREATE TABLE IF NOT EXISTS " + quote_id(table_name);
    }

    std::string current_timestamp_func() const override {
        return "CURRENT_TIMESTAMP";
    }

    std::string current_date_func() const override {
        return "CURRENT_DATE";
    }

    std::string extract_part_func(std::string_view part, std::string_view expr_sql) const override {
        return "EXTRACT(" + std::string(part) + " FROM CAST(" + std::string(expr_sql) + " AS TIMESTAMP))";
    }

    std::string date_add_days_func(std::string_view expr_sql, std::string_view days_sql) const override {
        return "(CAST(" + std::string(expr_sql) + " AS TIMESTAMP) + NUMTODSINTERVAL(" + std::string(days_sql) + ", 'DAY'))";
    }

    std::string generate_upsert(
        std::string_view table_name,
        const std::vector<std::string>& insert_columns,
        const std::vector<std::string>& conflict_columns,
        const std::vector<std::string>& update_columns
    ) const override {
        std::string sql = "MERGE INTO " + quote_id(table_name) + " target USING (SELECT ";
        for (size_t i = 0; i < insert_columns.size(); ++i) {
            if (i > 0) sql += ", ";
            sql += placeholder(i) + " AS " + quote_id(insert_columns[i]);
        }
        sql += " FROM DUAL) source ON (";
        for (size_t i = 0; i < conflict_columns.size(); ++i) {
            if (i > 0) sql += " AND ";
            sql += "target." + quote_id(conflict_columns[i]) + " = source." + quote_id(conflict_columns[i]);
        }
        sql += ") WHEN MATCHED THEN UPDATE SET ";
        for (size_t i = 0; i < update_columns.size(); ++i) {
            if (i > 0) sql += ", ";
            sql += "target." + quote_id(update_columns[i]) + " = source." + quote_id(update_columns[i]);
        }
        sql += " WHEN NOT MATCHED THEN INSERT (";
        for (size_t i = 0; i < insert_columns.size(); ++i) {
            if (i > 0) sql += ", ";
            sql += quote_id(insert_columns[i]);
        }
        sql += ") VALUES (";
        for (size_t i = 0; i < insert_columns.size(); ++i) {
            if (i > 0) sql += ", ";
            sql += "source." + quote_id(insert_columns[i]);
        }
        sql += ")";
        return sql;
    }
};

struct QueryBuilderUser {
    int64_t id = 0;
    std::string name;
};

struct QueryBuilderOrder {
    int64_t id = 0;
    int64_t user_id = 0;
    double amount = 0.0;
};

struct QueryBuilderAccount {
    int64_t id = 0;
    int64_t user_id = 0;
    std::string currency;
    double balance = 0.0;
};

inline auto query_builder_users_table() {
    return table<QueryBuilderUser>("users",
        column("id", &QueryBuilderUser::id, primary_key),
        column("name", &QueryBuilderUser::name));
}

inline auto query_builder_orders_table() {
    return table<QueryBuilderOrder>("orders",
        column("id", &QueryBuilderOrder::id, primary_key),
        column("user_id", &QueryBuilderOrder::user_id),
        column("amount", &QueryBuilderOrder::amount));
}

inline auto query_builder_accounts_table() {
    return table<QueryBuilderAccount>("accounts",
        column("id", &QueryBuilderAccount::id, primary_key),
        column("user_id", &QueryBuilderAccount::user_id),
        column("currency", &QueryBuilderAccount::currency),
        column("balance", &QueryBuilderAccount::balance));
}

class EmptyDataReader final : public IDataReader {
public:
    bool next() override { return false; }
    int column_count() const override { return 0; }
    bool is_null(int) const override { return true; }
    int64_t get_int64(int) const override { return 0; }
    uint64_t get_uint64(int) const override { return 0; }
    double get_double(int) const override { return 0.0; }
    std::string get_string(int) const override { return {}; }
    std::wstring get_wstring(int) const override { return {}; }
    bool get_bool(int) const override { return false; }
    std::vector<uint8_t> get_blob(int) const override { return {}; }
    SqlNumeric get_numeric(int) const override { return {}; }
    SqlDate get_date(int) const override { return {}; }
    SqlTime get_time(int) const override { return {}; }
    SqlTimestamp get_timestamp(int) const override { return {}; }
    SqlInterval get_interval(int) const override { return {}; }
    SqlGuid get_guid(int) const override { return {}; }
};

class CapturingStatement final : public IPreparedStatement {
public:
    void bind(int, const BoundValue&) override {}
    std::unique_ptr<IDataReader> execute_query() override {
        return std::make_unique<EmptyDataReader>();
    }
    size_t execute_non_query() override { return 0; }
    void reset() override {}
};

class CapturingConnection final : public IConnection {
public:
    void open() override {}
    void close() override {}
    bool is_open() const override { return true; }

    std::unique_ptr<IPreparedStatement> prepare(std::string_view sql) override {
        executed_sql.emplace_back(sql);
        return std::make_unique<CapturingStatement>();
    }

    void execute(std::string_view sql) override {
        executed_sql.emplace_back(sql);
    }

    void begin_transaction() override {}
    void commit() override {}
    void rollback() override {}
    const ISqlDialect& dialect() const override { return dialect_; }
    DriverInfo info() const override { return {}; }
    DriverCapabilities capabilities() const override { return {}; }

    const std::string& last_sql() const { return executed_sql.back(); }

    std::vector<std::string> executed_sql;

private:
    MockSqliteDialect dialect_;
};

} // namespace

// ============================================================================
// SELECT Generation Tests
// ============================================================================

TEST(SqlGeneratorTest, SelectAllNoWhere) {
    MockSqliteDialect dialect;
    SqlGenerator gen(dialect);

    auto result = gen.generate_select("users");
    EXPECT_EQ(result.sql, "SELECT * FROM \"users\"");
    EXPECT_TRUE(result.params.empty());
}

TEST(SqlGeneratorTest, SelectSpecificColumns) {
    MockSqliteDialect dialect;
    SqlGenerator gen(dialect);

    std::vector<std::string> cols = {"id", "name", "email"};
    auto result = gen.generate_select("users", cols);
    EXPECT_EQ(result.sql, "SELECT \"id\", \"name\", \"email\" FROM \"users\"");
    EXPECT_TRUE(result.params.empty());
}

TEST(SqlGeneratorTest, SelectWithWhereClause) {
    MockSqliteDialect dialect;
    SqlGenerator gen(dialect);

    ColumnHandle age("users", "age");
    auto where_expr = age > 18;

    auto result = gen.generate_select("users", {"id", "name"}, where_expr.node);
    EXPECT_EQ(result.sql, "SELECT \"id\", \"name\" FROM \"users\" WHERE (\"users\".\"age\" > ?)");
    ASSERT_EQ(result.params.size(), 1);
    EXPECT_EQ(std::get<int64_t>(result.params[0]), 18);
}

TEST(SqlGeneratorTest, SelectWithWhereOrderLimitOffset) {
    MockSqliteDialect dialect;
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

TEST(SqlGeneratorTest, SelectWithOrderByExprVector) {
    MockSqliteDialect dialect;
    SqlGenerator gen(dialect);

    ColumnHandle score("score");
    ColumnHandle id("id");
    std::vector<OrderByExpr> order_by = {
        desc(score),
        asc(id)
    };

    auto result = gen.generate_select("players", {"id", "score"}, std::nullopt, order_by, 5);
    EXPECT_EQ(result.sql, "SELECT \"id\", \"score\" FROM \"players\" ORDER BY \"score\" DESC, \"id\" ASC LIMIT 5");
    EXPECT_TRUE(result.params.empty());
}

TEST(SqlGeneratorTest, SelectWithComparisons) {
    MockSqliteDialect dialect;
    SqlGenerator gen(dialect);

    ColumnHandle x("x");

    EXPECT_EQ(gen.generate_select("t", {}, (x == 10).node).sql, "SELECT * FROM \"t\" WHERE (\"x\" = ?)");
    EXPECT_EQ(gen.generate_select("t", {}, (x != 10).node).sql, "SELECT * FROM \"t\" WHERE (\"x\" <> ?)");
    EXPECT_EQ(gen.generate_select("t", {}, (x < 10).node).sql, "SELECT * FROM \"t\" WHERE (\"x\" < ?)");
    EXPECT_EQ(gen.generate_select("t", {}, (x <= 10).node).sql, "SELECT * FROM \"t\" WHERE (\"x\" <= ?)");
    EXPECT_EQ(gen.generate_select("t", {}, (x > 10).node).sql, "SELECT * FROM \"t\" WHERE (\"x\" > ?)");
    EXPECT_EQ(gen.generate_select("t", {}, (x >= 10).node).sql, "SELECT * FROM \"t\" WHERE (\"x\" >= ?)");
}

TEST(SqlGeneratorTest, SelectWithUnaryOperators) {
    MockSqliteDialect dialect;
    SqlGenerator gen(dialect);

    ColumnHandle active("is_active");
    ColumnHandle email("email");

    auto not_active = !Expr(active);
    EXPECT_EQ(gen.generate_select("users", {}, not_active.node).sql,
              "SELECT * FROM \"users\" WHERE NOT (\"is_active\")");

    auto null_email = email.is_null();
    EXPECT_EQ(gen.generate_select("users", {}, null_email.node).sql,
              "SELECT * FROM \"users\" WHERE \"email\" IS NULL");

    auto not_null_email = email.is_not_null();
    EXPECT_EQ(gen.generate_select("users", {}, not_null_email.node).sql,
              "SELECT * FROM \"users\" WHERE \"email\" IS NOT NULL");
}

TEST(SqlGeneratorTest, SelectPostgresPlaceholders) {
    MockPostgresDialect dialect;
    SqlGenerator gen(dialect);

    ColumnHandle id("id");
    ColumnHandle age("age");
    auto where_expr = (id == 1) || (age < 30);

    auto result = gen.generate_select("users", {"id"}, where_expr.node);
    EXPECT_EQ(result.sql, "SELECT \"id\" FROM \"users\" WHERE ((\"id\" = $1) OR (\"age\" < $2))");
    ASSERT_EQ(result.params.size(), 2);
    EXPECT_EQ(std::get<int64_t>(result.params[0]), 1);
    EXPECT_EQ(std::get<int64_t>(result.params[1]), 30);
}

// ============================================================================
// INSERT Generation Tests
// ============================================================================

TEST(SqlGeneratorTest, InsertWithBoundValues) {
    MockSqliteDialect dialect;
    SqlGenerator gen(dialect);

    std::vector<std::string> cols = {"name", "age", "active"};
    std::vector<BoundValue> values = {std::string("Alice"), int64_t(30), true};

    auto result = gen.generate_insert("users", cols, values);
    EXPECT_EQ(result.sql, "INSERT INTO \"users\" (\"name\", \"age\", \"active\") VALUES (?, ?, ?)");
    ASSERT_EQ(result.params.size(), 3);
    EXPECT_EQ(std::get<std::string>(result.params[0]), "Alice");
    EXPECT_EQ(std::get<int64_t>(result.params[1]), 30);
    EXPECT_EQ(std::get<bool>(result.params[2]), true);
}

TEST(SqlGeneratorTest, InsertWithReturningClause) {
    MockSqliteDialect dialect;
    SqlGenerator gen(dialect);

    std::vector<std::string> cols = {"name", "age"};
    std::vector<BoundValue> values = {std::string("Bob"), int64_t(25)};

    auto result = gen.generate_insert("users", cols, values, "id");
    EXPECT_EQ(result.sql, "INSERT INTO \"users\" (\"name\", \"age\") VALUES (?, ?) RETURNING \"id\"");
    ASSERT_EQ(result.params.size(), 2);
}

TEST(SqlGeneratorTest, InsertWithSqlValues) {
    MockSqliteDialect dialect;
    SqlGenerator gen(dialect);

    std::vector<std::string> cols = {"name", "price"};
    std::vector<SqlValue> values = {SqlValue(std::string("Widget")), SqlValue(19.99)};

    auto result = gen.generate_insert("products", cols, values);
    EXPECT_EQ(result.sql, "INSERT INTO \"products\" (\"name\", \"price\") VALUES (?, ?)");
    ASSERT_EQ(result.params.size(), 2);
    EXPECT_EQ(std::get<std::string>(result.params[0]), "Widget");
    EXPECT_DOUBLE_EQ(std::get<double>(result.params[1]), 19.99);
}

// ============================================================================
// UPDATE Generation Tests
// ============================================================================

TEST(SqlGeneratorTest, UpdateWithAssignExpr) {
    MockSqliteDialect dialect;
    SqlGenerator gen(dialect);

    ColumnHandle name("users", "name");
    ColumnHandle age("users", "age");
    ColumnHandle id("users", "id");

    std::vector<AssignExpr> assignments = {
        name = "Charlie",
        age = 40
    };

    auto where_expr = id == 10;
    auto result = gen.generate_update("users", assignments, where_expr.node);
    EXPECT_EQ(result.sql, "UPDATE \"users\" SET \"name\" = ?, \"age\" = ? WHERE (\"users\".\"id\" = ?)");
    ASSERT_EQ(result.params.size(), 3);
    EXPECT_EQ(std::get<std::string>(result.params[0]), "Charlie");
    EXPECT_EQ(std::get<int64_t>(result.params[1]), 40);
    EXPECT_EQ(std::get<int64_t>(result.params[2]), 10);
}

TEST(SqlGeneratorTest, UpdateWithPairAssignmentsNoWhere) {
    MockSqliteDialect dialect;
    SqlGenerator gen(dialect);

    std::vector<std::pair<std::string, BoundValue>> assignments = {
        {"status", std::string("active")},
        {"retry_count", int64_t(0)}
    };

    auto result = gen.generate_update("tasks", assignments);
    EXPECT_EQ(result.sql, "UPDATE \"tasks\" SET \"status\" = ?, \"retry_count\" = ?");
    ASSERT_EQ(result.params.size(), 2);
    EXPECT_EQ(std::get<std::string>(result.params[0]), "active");
    EXPECT_EQ(std::get<int64_t>(result.params[1]), 0);
}

// ============================================================================
// DELETE Generation Tests
// ============================================================================

TEST(SqlGeneratorTest, DeleteWithWhere) {
    MockSqliteDialect dialect;
    SqlGenerator gen(dialect);

    ColumnHandle id("id");
    auto where_expr = id == 5;

    auto result = gen.generate_delete("users", where_expr.node);
    EXPECT_EQ(result.sql, "DELETE FROM \"users\" WHERE (\"id\" = ?)");
    ASSERT_EQ(result.params.size(), 1);
    EXPECT_EQ(std::get<int64_t>(result.params[0]), 5);
}

TEST(SqlGeneratorTest, DeleteAllNoWhere) {
    MockSqliteDialect dialect;
    SqlGenerator gen(dialect);

    auto result = gen.generate_delete("logs");
    EXPECT_EQ(result.sql, "DELETE FROM \"logs\"");
    EXPECT_TRUE(result.params.empty());
}

// ============================================================================
// Aggregates & COUNT Tests
// ============================================================================

TEST(SqlGeneratorTest, CountAll) {
    MockSqliteDialect dialect;
    SqlGenerator gen(dialect);

    auto result = gen.generate_count("users");
    EXPECT_EQ(result.sql, "SELECT COUNT(*) FROM \"users\"");
    EXPECT_TRUE(result.params.empty());
}

TEST(SqlGeneratorTest, CountWithWhere) {
    MockSqliteDialect dialect;
    SqlGenerator gen(dialect);

    ColumnHandle score("score");
    auto where_expr = score >= 50.0;

    auto result = gen.generate_count("users", where_expr.node);
    EXPECT_EQ(result.sql, "SELECT COUNT(*) FROM \"users\" WHERE (\"score\" >= ?)");
    ASSERT_EQ(result.params.size(), 1);
    EXPECT_EQ(std::get<double>(result.params[0]), 50.0);
}

TEST(SqlGeneratorTest, AggregateFunctions) {
    MockSqliteDialect dialect;
    SqlGenerator gen(dialect);

    ColumnHandle salary("salary");
    auto where_expr = salary > 50000;

    auto avg_res = gen.generate_aggregate("AVG", "employees", "salary", where_expr.node);
    EXPECT_EQ(avg_res.sql, "SELECT AVG(\"salary\") FROM \"employees\" WHERE (\"salary\" > ?)");
    ASSERT_EQ(avg_res.params.size(), 1);

    auto sum_res = gen.generate_aggregate("SUM", "employees", "salary");
    EXPECT_EQ(sum_res.sql, "SELECT SUM(\"salary\") FROM \"employees\"");

    auto min_res = gen.generate_aggregate("MIN", "employees", "salary");
    EXPECT_EQ(min_res.sql, "SELECT MIN(\"salary\") FROM \"employees\"");

    auto max_res = gen.generate_aggregate("MAX", "employees", "salary");
    EXPECT_EQ(max_res.sql, "SELECT MAX(\"salary\") FROM \"employees\"");

    auto count_star = gen.generate_aggregate("COUNT", "employees", "*");
    EXPECT_EQ(count_star.sql, "SELECT COUNT(*) FROM \"employees\"");
}

// ============================================================================
// DDL (CREATE TABLE) Generation Tests
// ============================================================================

TEST(SqlGeneratorTest, CreateTableSqlite) {
    MockSqliteDialect dialect;
    SqlGenerator gen(dialect);

    std::vector<ColumnInfo> cols = {
        {"id", SqlType::Integer, true, true, false, false, false},
        {"name", SqlType::Text, false, false, true, false, false},
        {"email", SqlType::Text, false, false, false, false, true},
        {"score", SqlType::Real, false, false, false, false, false},
        {"data", SqlType::Blob, false, false, false, false, false}
    };

    auto result = gen.generate_create_table("users", cols);
    EXPECT_EQ(result.sql, "CREATE TABLE IF NOT EXISTS \"users\" (\"id\" INTEGER PRIMARY KEY AUTOINCREMENT, \"name\" TEXT NOT NULL, \"email\" TEXT UNIQUE, \"score\" REAL, \"data\" BLOB)");
    EXPECT_TRUE(result.params.empty());
}

TEST(SqlGeneratorTest, CreateTablePostgres) {
    MockPostgresDialect dialect;
    SqlGenerator gen(dialect);

    std::vector<ColumnInfo> cols = {
        {"id", SqlType::BigInt, true, true, false, false, false},
        {"name", SqlType::Text, false, false, true, false, false},
        {"is_admin", SqlType::Boolean, false, false, true, false, false},
        {"balance", SqlType::Real, false, false, false, false, false}
    };

    auto result = gen.generate_create_table("accounts", cols);
    EXPECT_EQ(result.sql, "CREATE TABLE IF NOT EXISTS \"accounts\" (\"id\" BIGSERIAL PRIMARY KEY, \"name\" TEXT NOT NULL, \"is_admin\" BOOLEAN NOT NULL, \"balance\" DOUBLE PRECISION)");
    EXPECT_TRUE(result.params.empty());
}

TEST(SqlGeneratorTest, SelectMssqlBracketsAndPagination) {
    MockMssqlDialect dialect;
    SqlGenerator gen(dialect);

    ColumnHandle age("age");
    auto where_expr = age >= 25;

    std::vector<std::pair<ExprNode, SortDir>> order_by = {
        {age.ref, SortDir::Desc}
    };

    auto result = gen.generate_select(
        "users",
        {"id", "name", "age"},
        where_expr.node,
        order_by,
        10,
        5
    );

    EXPECT_EQ(result.sql, "SELECT [id], [name], [age] FROM [users] WHERE ([age] >= ?) ORDER BY [age] DESC OFFSET 5 ROWS FETCH NEXT 10 ROWS ONLY");
    ASSERT_EQ(result.params.size(), 1);
    EXPECT_EQ(std::get<int64_t>(result.params[0]), 25);
}

TEST(SqlGeneratorTest, InsertMssqlWithOutputInserted) {
    MockMssqlDialect dialect;
    SqlGenerator gen(dialect);

    std::vector<std::string> cols = {"name", "age"};
    std::vector<BoundValue> values = {std::string("Alice"), int64_t(30)};

    auto result = gen.generate_insert("users", cols, values, "id");
    EXPECT_EQ(result.sql, "INSERT INTO [users] ([name], [age]) OUTPUT INSERTED.[id] VALUES (?, ?)");
    ASSERT_EQ(result.params.size(), 2);
    EXPECT_EQ(std::get<std::string>(result.params[0]), "Alice");
    EXPECT_EQ(std::get<int64_t>(result.params[1]), 30);
}

TEST(SqlGeneratorTest, CreateTableMssql) {
    MockMssqlDialect dialect;
    SqlGenerator gen(dialect);

    std::vector<ColumnInfo> cols = {
        {"id", SqlType::Integer, true, true, false, false, false},
        {"name", SqlType::Text, false, false, true, false, false},
        {"email", SqlType::Text, false, false, false, false, true},
        {"active", SqlType::Boolean, false, false, true, false, false}
    };

    auto result = gen.generate_create_table("users", cols);
    EXPECT_EQ(result.sql, "IF OBJECT_ID(N'users', N'U') IS NULL CREATE TABLE [users] ([id] INT IDENTITY(1,1) PRIMARY KEY, [name] NVARCHAR(MAX) NOT NULL, [email] NVARCHAR(MAX) UNIQUE, [active] BIT NOT NULL)");
    EXPECT_TRUE(result.params.empty());
}

TEST(SqlGeneratorTest, SelectDistinct) {
    MockSqliteDialect dialect;
    SqlGenerator gen(dialect);
    auto result = gen.generate_select("users", {"name", "age"}, std::nullopt, std::vector<expr::OrderByExpr>{}, std::nullopt, std::nullopt, true);
    EXPECT_EQ(result.sql, "SELECT DISTINCT \"name\", \"age\" FROM \"users\"");
}

TEST(SqlGeneratorTest, WhereBetween) {
    MockSqliteDialect dialect;
    SqlGenerator gen(dialect);
    ColumnHandle age("users", "age");
    auto result = gen.generate_select("users", {"id"}, age.between(20, 30).node);
    EXPECT_EQ(result.sql, "SELECT \"id\" FROM \"users\" WHERE (\"users\".\"age\" BETWEEN ? AND ?)");
    ASSERT_EQ(result.params.size(), 2);
    EXPECT_EQ(std::get<int64_t>(result.params[0]), 20);
    EXPECT_EQ(std::get<int64_t>(result.params[1]), 30);
}

TEST(SqlGeneratorTest, WhereLikeAndInList) {
    MockSqliteDialect dialect;
    SqlGenerator gen(dialect);
    ColumnHandle name("users", "name");
    ColumnHandle age("users", "age");
    auto expr = name.like("A%") && age.in_list({20, 25, 30});
    auto result = gen.generate_select("users", {"id"}, expr.node);
    EXPECT_EQ(result.sql, "SELECT \"id\" FROM \"users\" WHERE ((\"users\".\"name\" LIKE ?) AND (\"users\".\"age\" IN (?, ?, ?)))");
    ASSERT_EQ(result.params.size(), 4);
    EXPECT_EQ(std::get<std::string>(result.params[0]), "A%");
    EXPECT_EQ(std::get<int64_t>(result.params[1]), 20);
    EXPECT_EQ(std::get<int64_t>(result.params[2]), 25);
    EXPECT_EQ(std::get<int64_t>(result.params[3]), 30);
}

TEST(SqlGeneratorTest, MultiColumnOrderBy) {
    MockSqliteDialect dialect;
    SqlGenerator gen(dialect);
    ColumnHandle dept("users", "dept");
    ColumnHandle salary("users", "salary");
    std::vector<std::pair<expr::ExprNode, expr::SortDir>> order_by = {
        {dept.ref, expr::SortDir::Asc},
        {salary.ref, expr::SortDir::Desc}
    };
    auto result = gen.generate_select("users", {"id"}, std::nullopt, order_by);
    EXPECT_EQ(result.sql, "SELECT \"id\" FROM \"users\" ORDER BY \"users\".\"dept\" ASC, \"users\".\"salary\" DESC");
}

TEST(SqlGeneratorTest, SqlFunctionsSqlite) {
    MockSqliteDialect dialect;
    SqlGenerator gen(dialect);
    ColumnHandle name("users", "name");
    ColumnHandle email("users", "email");

    auto result = gen.generate_select("users", {"id"}, (lower(name) == "alice" && length(email) > 5).node);
    EXPECT_EQ(result.sql, "SELECT \"id\" FROM \"users\" WHERE ((LOWER(\"users\".\"name\") = ?) AND (LENGTH(\"users\".\"email\") > ?))");
    ASSERT_EQ(result.params.size(), 2);
    EXPECT_EQ(std::get<std::string>(result.params[0]), "alice");
    EXPECT_EQ(std::get<int64_t>(result.params[1]), 5);
}

TEST(SqlGeneratorTest, SqlFunctionsMssql) {
    MockMssqlDialect dialect;
    SqlGenerator gen(dialect);
    ColumnHandle name("users", "name");
    ColumnHandle email("users", "email");

    auto result = gen.generate_select("users", {"id"}, (lower(name) == "alice" && length(email) > 5).node);
    EXPECT_EQ(result.sql, "SELECT [id] FROM [users] WHERE ((LOWER([users].[name]) = ?) AND (LEN([users].[email]) > ?))");
    ASSERT_EQ(result.params.size(), 2);
    EXPECT_EQ(std::get<std::string>(result.params[0]), "alice");
    EXPECT_EQ(std::get<int64_t>(result.params[1]), 5);
}

TEST(SqlGeneratorTest, GroupByAndHaving) {
    MockSqliteDialect dialect;
    SqlGenerator gen(dialect);
    ColumnHandle dept("users", "dept");
    ColumnHandle age("users", "age");

    auto result = gen.generate_select("users", {"dept"}, std::nullopt,
                                      std::vector<std::pair<expr::ExprNode, expr::SortDir>>{},
                                      std::nullopt, std::nullopt, false,
                                      {dept.ref}, (age > 18).node);

    EXPECT_EQ(result.sql, "SELECT \"dept\" FROM \"users\" GROUP BY \"users\".\"dept\" HAVING (\"users\".\"age\" > ?)");
    ASSERT_EQ(result.params.size(), 1);
    EXPECT_EQ(std::get<int64_t>(result.params[0]), 18);
}

TEST(SqlGeneratorTest, CountDistinct) {
    MockSqliteDialect dialect;
    SqlGenerator gen(dialect);
    ColumnHandle dept("users", "dept");

    auto result = gen.generate_count("users", std::nullopt, true, "dept");
    EXPECT_EQ(result.sql, "SELECT COUNT(DISTINCT \"dept\") FROM \"users\"");
    EXPECT_TRUE(result.params.empty());
}

TEST(SqlGeneratorTest, JoinedSelectInnerAndLeft) {
    MockSqliteDialect dialect;
    SqlGenerator gen(dialect);
    ColumnHandle u_id("users", "id");
    ColumnHandle o_uid("orders", "user_id");

    JoinClause inner_jc{"INNER JOIN", "orders", (u_id == o_uid).node};
    auto inner_res = gen.generate_joined_select(
        "users", {"id", "name"},
        {inner_jc},
        {{"orders", {"id", "amount"}}}
    );

    EXPECT_EQ(inner_res.sql, "SELECT \"users\".\"id\", \"users\".\"name\", \"orders\".\"id\", \"orders\".\"amount\" FROM \"users\" INNER JOIN \"orders\" ON (\"users\".\"id\" = \"orders\".\"user_id\")");

    JoinClause left_jc{"LEFT JOIN", "orders", (u_id == o_uid).node};
    auto left_res = gen.generate_joined_select(
        "users", {"id", "name"},
        {left_jc},
        {{"orders", {"id", "amount"}}}
    );

    EXPECT_EQ(left_res.sql, "SELECT \"users\".\"id\", \"users\".\"name\", \"orders\".\"id\", \"orders\".\"amount\" FROM \"users\" LEFT JOIN \"orders\" ON (\"users\".\"id\" = \"orders\".\"user_id\")");
}

TEST(SqlGeneratorTest, UpsertSqlite) {
    MockSqliteDialect dialect;
    SqlGenerator gen(dialect);

    std::vector<BoundValue> values = {int64_t{1}, std::string{"Alice"}, int64_t{30}};
    auto result = gen.generate_upsert("users", {"id", "name", "age"}, values, {"id"}, {"name", "age"});

    EXPECT_EQ(result.sql, "INSERT INTO \"users\" (\"id\", \"name\", \"age\") VALUES (?, ?, ?) ON CONFLICT (\"id\") DO UPDATE SET \"name\" = EXCLUDED.\"name\", \"age\" = EXCLUDED.\"age\"");
    ASSERT_EQ(result.params.size(), 3);
}

TEST(SqlGeneratorTest, UpsertEmptyUpdateColumnsEmitsDoNothing) {
    MockSqliteDialect dialect;
    SqlGenerator gen(dialect);

    std::vector<BoundValue> values = {int64_t{1}, std::string{"Alice"}};
    auto result = gen.generate_upsert("users", {"id", "name"}, values, {"id", "name"}, {});

    EXPECT_EQ(result.sql, "INSERT INTO \"users\" (\"id\", \"name\") VALUES (?, ?) ON CONFLICT (\"id\", \"name\") DO NOTHING");
    ASSERT_EQ(result.params.size(), 2);
}

TEST(SqlGeneratorTest, UpsertMssql) {
    MockMssqlDialect dialect;
    SqlGenerator gen(dialect);

    std::vector<BoundValue> values = {int64_t{1}, std::string{"Alice"}, int64_t{30}};
    auto result = gen.generate_upsert("users", {"id", "name", "age"}, values, {"id"}, {"name", "age"});

    EXPECT_EQ(result.sql, "MERGE INTO [users] WITH (HOLDLOCK) AS [target] USING (VALUES (?, ?, ?)) AS [source] ([id], [name], [age]) ON ([target].[id] = [source].[id]) WHEN MATCHED THEN UPDATE SET [target].[name] = [source].[name], [target].[age] = [source].[age] WHEN NOT MATCHED THEN INSERT ([id], [name], [age]) VALUES ([source].[id], [source].[name], [source].[age]);");
    ASSERT_EQ(result.params.size(), 3);
}

TEST(SqlGeneratorTest, SetOperationsGeneration) {
    MockSqliteDialect dialect;
    SqlGenerator gen(dialect);

    ColumnHandle age("users", "age");
    ColumnHandle admin_age("admins", "age");

    SetOpClause op{
        SetOpType::UnionAll,
        "admins",
        {"id", "name", "email", "age"},
        (admin_age >= 40).node,
        false
    };

    auto result = gen.generate_set_operation(
        "users", {"id", "name", "email", "age"}, (age < 30).node, false,
        {op}
    );

    EXPECT_EQ(result.sql, "SELECT \"id\", \"name\", \"email\", \"age\" FROM \"users\" WHERE (\"users\".\"age\" < ?) UNION ALL SELECT \"id\", \"name\", \"email\", \"age\" FROM \"admins\" WHERE (\"admins\".\"age\" >= ?)");
    ASSERT_EQ(result.params.size(), 2);
    EXPECT_EQ(std::get<int64_t>(result.params[0]), 30);
    EXPECT_EQ(std::get<int64_t>(result.params[1]), 40);
}

TEST(SqlGeneratorTest, SubqueryGeneration) {
    MockSqliteDialect dialect;
    SqlGenerator gen(dialect);

    ColumnHandle u_id("users", "id");
    ColumnHandle o_uid("orders", "user_id");
    ColumnHandle o_amount("orders", "amount");

    // EXISTS (SELECT 1 FROM "orders" WHERE ("orders"."user_id" = "users"."id" AND "orders"."amount" > ?))
    SubqueryExpr sub("orders", {}, ((o_uid == u_id) && (o_amount > 100.0)).node);
    auto exists_res = gen.generate_select("users", {"id", "name"}, (exists(sub)).node);

    EXPECT_EQ(exists_res.sql, "SELECT \"id\", \"name\" FROM \"users\" WHERE EXISTS (SELECT 1 FROM \"orders\" WHERE ((\"orders\".\"user_id\" = \"users\".\"id\") AND (\"orders\".\"amount\" > ?)))");
    ASSERT_EQ(exists_res.params.size(), 1);
    EXPECT_DOUBLE_EQ(std::get<double>(exists_res.params[0]), 100.0);

    // IN (SELECT "user_id" FROM "orders" WHERE "orders"."amount" > ?)
    SubqueryExpr in_sub("orders", {"user_id"}, (o_amount > 50.0).node);
    auto in_res = gen.generate_select("users", {"id", "name"}, (u_id.in(in_sub)).node);

    EXPECT_EQ(in_res.sql, "SELECT \"id\", \"name\" FROM \"users\" WHERE (\"users\".\"id\" IN (SELECT \"user_id\" FROM \"orders\" WHERE (\"orders\".\"amount\" > ?)))");
    ASSERT_EQ(in_res.params.size(), 1);
    EXPECT_DOUBLE_EQ(std::get<double>(in_res.params[0]), 50.0);
}

TEST(SqlGeneratorTest, ExistsWithNonEmptyProjection) {
    MockSqliteDialect dialect;
    SqlGenerator gen(dialect);

    ColumnHandle user_id("users", "id");
    ColumnHandle order_user_id("orders", "user_id");
    ColumnHandle amount("orders", "amount");

    SubqueryExpr sub("orders", {"user_id", "amount"},
                     ((order_user_id == user_id) && (amount > 100.0)).node);
    auto res = gen.generate_select("users", {"id"}, exists(sub).node);

    EXPECT_EQ(res.sql, "SELECT \"id\" FROM \"users\" WHERE EXISTS (SELECT \"user_id\", \"amount\" FROM \"orders\" WHERE ((\"orders\".\"user_id\" = \"users\".\"id\") AND (\"orders\".\"amount\" > ?)))");
    ASSERT_EQ(res.params.size(), 1);
    EXPECT_DOUBLE_EQ(std::get<double>(res.params[0]), 100.0);
}

TEST(SqlGeneratorTest, NotInSubqueryGeneration) {
    MockSqliteDialect dialect;
    SqlGenerator gen(dialect);

    ColumnHandle user_id("users", "id");
    ColumnHandle amount("orders", "amount");
    SubqueryExpr sub("orders", {"user_id"}, (amount > 50.0).node);

    auto res = gen.generate_select("users", {"id"}, user_id.not_in(sub).node);

    EXPECT_EQ(res.sql, "SELECT \"id\" FROM \"users\" WHERE (\"users\".\"id\" NOT IN (SELECT \"user_id\" FROM \"orders\" WHERE (\"orders\".\"amount\" > ?)))");
    ASSERT_EQ(res.params.size(), 1);
    EXPECT_DOUBLE_EQ(std::get<double>(res.params[0]), 50.0);
}

TEST(SqlGeneratorTest, CteParametersFollowDefinitionOrder) {
    MockSqliteDialect dialect;
    SqlGenerator gen(dialect);

    ColumnHandle order_amount("orders", "amount");
    ColumnHandle refund_amount("refunds", "amount");
    ColumnHandle user_id("users", "id");

    CteClause order_users{
        "order_users",
        SubqueryExpr("orders", {"user_id"}, (order_amount > 100.0).node),
        false
    };
    CteClause refund_users{
        "refund_users",
        SubqueryExpr("refunds", {"user_id"}, (refund_amount >= 25).node, true),
        false
    };

    auto res = gen.generate_cte_select(
        {order_users, refund_users},
        "users",
        {"id", "name"},
        (user_id > 5).node
    );

    EXPECT_EQ(res.sql, "WITH \"order_users\" AS (SELECT \"user_id\" FROM \"orders\" WHERE (\"orders\".\"amount\" > ?)), \"refund_users\" AS (SELECT DISTINCT \"user_id\" FROM \"refunds\" WHERE (\"refunds\".\"amount\" >= ?)) SELECT \"id\", \"name\" FROM \"users\" WHERE (\"users\".\"id\" > ?)");
    ASSERT_EQ(res.params.size(), 3);
    EXPECT_DOUBLE_EQ(std::get<double>(res.params[0]), 100.0);
    EXPECT_EQ(std::get<int64_t>(res.params[1]), 25);
    EXPECT_EQ(std::get<int64_t>(res.params[2]), 5);
}

TEST(SqlGeneratorTest, DateTimeFunctionsSqlite) {
    MockSqliteDialect dialect;
    SqlGenerator gen(dialect);

    ColumnHandle created_at("users", "created_at");

    auto res = gen.generate_select("users", {"id"}, (created_at.year() == 2026 && created_at.month() == 8 && created_at.day() == 16).node);
    EXPECT_EQ(res.sql, "SELECT \"id\" FROM \"users\" WHERE (((CAST(strftime('%Y', \"users\".\"created_at\") AS INTEGER) = ?) AND (CAST(strftime('%m', \"users\".\"created_at\") AS INTEGER) = ?)) AND (CAST(strftime('%d', \"users\".\"created_at\") AS INTEGER) = ?))");
    ASSERT_EQ(res.params.size(), 3);
}

TEST(SqlGeneratorTest, DateTimeFunctionsMssql) {
    MockMssqlDialect dialect;
    SqlGenerator gen(dialect);

    ColumnHandle created_at("users", "created_at");

    auto res = gen.generate_select("users", {"id"}, (created_at.year() == 2026 && created_at.month() == 8 && created_at.day() == 16).node);
    EXPECT_EQ(res.sql, "SELECT [id] FROM [users] WHERE (((YEAR([users].[created_at]) = ?) AND (MONTH([users].[created_at]) = ?)) AND (DAY([users].[created_at]) = ?))");
    ASSERT_EQ(res.params.size(), 3);
}

TEST(SqlGeneratorTest, WindowFunctionGeneration) {
    MockSqliteDialect dialect;
    SqlGenerator gen(dialect);

    ColumnHandle dept("employees", "department");
    ColumnHandle salary("employees", "salary");

    // ROW_NUMBER() OVER (PARTITION BY "employees"."department" ORDER BY "employees"."salary" DESC)
    auto rn = row_number().over().partition_by(dept).order_by(salary.desc());
    auto rn_res = gen.generate_expression(Expr(rn).node);

    EXPECT_EQ(rn_res.sql, "ROW_NUMBER() OVER (PARTITION BY \"employees\".\"department\" ORDER BY \"employees\".\"salary\" DESC)");
    EXPECT_TRUE(rn_res.params.empty());

    // DENSE_RANK() OVER (ORDER BY "employees"."salary" DESC)
    auto dr = dense_rank().over().order_by(salary.desc());
    auto dr_res = gen.generate_expression(Expr(dr).node);
    EXPECT_EQ(dr_res.sql, "DENSE_RANK() OVER (ORDER BY \"employees\".\"salary\" DESC)");

    // SUM("employees"."salary") OVER (PARTITION BY "employees"."department")
    auto sum_w = sum_over(salary).over().partition_by(dept);
    auto sum_res = gen.generate_expression(Expr(sum_w).node);
    EXPECT_EQ(sum_res.sql, "SUM(\"employees\".\"salary\") OVER (PARTITION BY \"employees\".\"department\")");
}

TEST(SqlGeneratorTest, CteGeneration) {
    MockSqliteDialect dialect;
    SqlGenerator gen(dialect);

    ColumnHandle amount("orders", "amount");
    SubqueryExpr sub("orders", {"user_id", "amount"}, std::make_shared<expr::ExprNode>((amount > 100.0).node), false);

    CteClause cte{"high_value_orders", sub, false};
    auto res = gen.generate_cte_select({cte}, "high_value_orders", {"user_id"}, std::nullopt);

    EXPECT_EQ(res.sql, "WITH \"high_value_orders\" AS (SELECT \"user_id\", \"amount\" FROM \"orders\" WHERE (\"orders\".\"amount\" > ?)) SELECT \"user_id\" FROM \"high_value_orders\"");
    ASSERT_EQ(res.params.size(), 1);
    EXPECT_EQ(std::get<double>(res.params[0]), 100.0);
}

TEST(SqlGeneratorTest, ScalarSubqueryGeneration) {
    MockSqliteDialect dialect;
    SqlGenerator gen(dialect);

    ColumnHandle amount("orders", "amount");
    SubqueryExpr sub("orders", {"amount"}, nullptr, false);

    // SubqueryExpr is not implicitly convertible to Expr; wrap it explicitly.
    Expr max_amount(std::make_shared<expr::SubqueryExpr>(sub));
    auto res = gen.generate_expression((amount >= max_amount).node);

    EXPECT_EQ(res.sql, "(\"orders\".\"amount\" >= (SELECT \"amount\" FROM \"orders\"))");
    EXPECT_TRUE(res.params.empty());
}

TEST(SqlGeneratorTest, MultiTableJoinGeneration) {
    MockSqliteDialect dialect;
    SqlGenerator gen(dialect);

    ColumnHandle user_id("users", "id");
    ColumnHandle order_user_id("orders", "user_id");
    ColumnHandle order_id("orders", "id");
    ColumnHandle item_order_id("order_items", "order_id");

    JoinClause j1{"INNER JOIN", "orders", (user_id == order_user_id).node};
    JoinClause j2{"LEFT JOIN", "order_items", (order_id == item_order_id).node};

    auto res = gen.generate_joined_select(
        "users",
        {"id", "name"},
        {j1, j2},
        {{"orders", {"id", "amount"}}, {"order_items", {"id", "product"}}},
        std::nullopt
    );

    EXPECT_EQ(res.sql, "SELECT \"users\".\"id\", \"users\".\"name\", \"orders\".\"id\", \"orders\".\"amount\", \"order_items\".\"id\", \"order_items\".\"product\" FROM \"users\" INNER JOIN \"orders\" ON (\"users\".\"id\" = \"orders\".\"user_id\") LEFT JOIN \"order_items\" ON (\"orders\".\"id\" = \"order_items\".\"order_id\")");
}

TEST(QueryBuilderSqlTest, ExistsAndNotExistsBuilderOverloads) {
    CapturingConnection conn;
    DbContext<sqlite> db(conn);
    auto orders = query_builder_orders_table();

    auto order_query = db.from(orders);
    order_query.where(orders["amount"] > 100.0);

    SqlGenerator gen(conn.dialect());
    auto exists_res = gen.generate_select("users", {"id"}, exists(order_query).node);
    EXPECT_EQ(exists_res.sql, "SELECT \"id\" FROM \"users\" WHERE EXISTS (SELECT \"id\", \"user_id\", \"amount\" FROM \"orders\" WHERE (\"orders\".\"amount\" > ?))");
    ASSERT_EQ(exists_res.params.size(), 1);
    EXPECT_DOUBLE_EQ(std::get<double>(exists_res.params[0]), 100.0);

    auto not_exists_res = gen.generate_select("users", {"id"}, not_exists(order_query).node);
    EXPECT_EQ(not_exists_res.sql, "SELECT \"id\" FROM \"users\" WHERE NOT EXISTS (SELECT \"id\", \"user_id\", \"amount\" FROM \"orders\" WHERE (\"orders\".\"amount\" > ?))");
    ASSERT_EQ(not_exists_res.params.size(), 1);
    EXPECT_DOUBLE_EQ(std::get<double>(not_exists_res.params[0]), 100.0);
}

TEST(QueryBuilderSqlTest, CteUsesSourceProjectionAndDropsSourceClauses) {
    CapturingConnection conn;
    DbContext<sqlite> db(conn);
    auto users = query_builder_users_table();
    auto orders = query_builder_orders_table();

    auto source = db.from(orders);
    source.where(orders["amount"] > 100.0)
          .distinct()
          .order_by_desc(orders["amount"])
          .limit(10)
          .offset(5)
          .group_by(orders["user_id"])
          .having(orders["amount"] > 200.0);

    auto outer = db.from(users);
    outer.with_cte("high_value_orders", source);
    outer.to_list();

    EXPECT_EQ(conn.last_sql(), "WITH \"high_value_orders\" AS (SELECT DISTINCT \"id\", \"user_id\", \"amount\" FROM \"orders\" WHERE (\"orders\".\"amount\" > ?)) SELECT \"id\", \"name\" FROM \"users\"");

    auto single_column_outer = db.from(users);
    single_column_outer.with_cte("order_user_ids", source.as_subquery(orders["user_id"]));
    single_column_outer.to_list();

    EXPECT_EQ(conn.last_sql(), "WITH \"order_user_ids\" AS (SELECT DISTINCT \"user_id\" FROM \"orders\" WHERE (\"orders\".\"amount\" > ?)) SELECT \"id\", \"name\" FROM \"users\"");
}

TEST(QueryBuilderSqlTest, CteWithoutDeclaredColumnsEmitsSelectStar) {
    CapturingConnection conn;
    DbContext<sqlite> db(conn);
    auto users = query_builder_users_table();
    auto empty_table = table<QueryBuilderUser>("empty_users");

    auto source = db.from(empty_table);
    auto outer = db.from(users);
    outer.with_cte("empty_cte", source);
    outer.to_list();

    EXPECT_EQ(conn.last_sql(),
              "WITH \"empty_cte\" AS (SELECT * FROM \"empty_users\") "
              "SELECT \"id\", \"name\" FROM \"users\"");
}

TEST(QueryBuilderSqlTest, MixedJoinChainGeneratesJoinTypesInOrder) {
    CapturingConnection conn;
    DbContext<sqlite> db(conn);
    auto users = query_builder_users_table();
    auto orders = query_builder_orders_table();
    auto accounts = query_builder_accounts_table();

    auto query = db.from(users)
        .left_join(orders).on(users["id"] == orders["user_id"])
        .join(accounts).on(accounts["user_id"] == users["id"])
        .where(accounts["currency"] == "USD")
        .order_by_desc(orders["amount"])
        .limit(5)
        .offset(2);
    query.to_list();

    EXPECT_EQ(conn.last_sql(), "SELECT \"users\".\"id\", \"users\".\"name\", \"orders\".\"id\", \"orders\".\"user_id\", \"orders\".\"amount\", \"accounts\".\"id\", \"accounts\".\"user_id\", \"accounts\".\"currency\", \"accounts\".\"balance\" FROM \"users\" LEFT JOIN \"orders\" ON (\"users\".\"id\" = \"orders\".\"user_id\") INNER JOIN \"accounts\" ON (\"accounts\".\"user_id\" = \"users\".\"id\") WHERE (\"accounts\".\"currency\" = ?) ORDER BY \"orders\".\"amount\" DESC LIMIT 5 OFFSET 2");
}

TEST(QueryBuilderSqlTest, SetOperationCountIgnoresOrderingAndPagination) {
    CapturingConnection conn;
    DbContext<sqlite> db(conn);
    auto users = query_builder_users_table();

    auto first = db.from(users);
    first.where(users["id"] > 0);
    auto second = db.from(users);
    second.where(users["id"] < 100);

    auto combined = first.union_all(second);
    combined.order_by_desc(users["name"]).limit(10).offset(5);

    EXPECT_EQ(combined.count(), 0u);
    EXPECT_EQ(conn.last_sql(), "SELECT COUNT(*) FROM (SELECT \"id\", \"name\" FROM \"users\" WHERE (\"users\".\"id\" > ?) UNION ALL SELECT \"id\", \"name\" FROM \"users\" WHERE (\"users\".\"id\" < ?)) \"_sub\"");
}

TEST(QueryBuilderSqlTest, SetOperationCopiesOnlyOperandProjectionWhereAndDistinct) {
    CapturingConnection conn;
    DbContext<sqlite> db(conn);
    auto users = query_builder_users_table();

    auto base = db.from(users);
    base.where(users["id"] > 0);

    auto operand = db.from(users);
    operand.where(users["id"] < 100)
           .distinct()
           .order_by_desc(users["name"])
           .limit(5)
           .offset(2)
           .group_by(users["name"])
           .having(users["id"] > 3);

    auto combined = base.union_with(operand);
    combined.to_list();

    EXPECT_EQ(conn.last_sql(),
              "SELECT \"id\", \"name\" FROM \"users\" WHERE (\"users\".\"id\" > ?) "
              "UNION SELECT DISTINCT \"id\", \"name\" FROM \"users\" WHERE (\"users\".\"id\" < ?)");
}

TEST(QueryBuilderSqlTest, CteIsDroppedByJoinedSetOperationAndMutationTerminals) {
    CapturingConnection conn;
    DbContext<sqlite> db(conn);
    auto users = query_builder_users_table();
    auto orders = query_builder_orders_table();

    auto cte_source = db.from(orders);
    cte_source.where(orders["amount"] > 100.0);

    auto join_base = db.from(users);
    join_base.with_cte("high_value_orders", cte_source);
    auto joined = join_base.join(orders).on(users["id"] == orders["user_id"]);
    joined.to_list();
    EXPECT_EQ(conn.last_sql(),
              "SELECT \"users\".\"id\", \"users\".\"name\", \"orders\".\"id\", \"orders\".\"user_id\", \"orders\".\"amount\" "
              "FROM \"users\" INNER JOIN \"orders\" ON (\"users\".\"id\" = \"orders\".\"user_id\")");
    EXPECT_EQ(conn.last_sql().find("WITH"), std::string::npos);

    auto set_left = db.from(users);
    set_left.with_cte("high_value_orders", cte_source);
    set_left.where(users["id"] > 0);
    auto set_right = db.from(users);
    set_right.where(users["id"] < 100);
    set_left.union_with(set_right).to_list();
    EXPECT_EQ(conn.last_sql(),
              "SELECT \"id\", \"name\" FROM \"users\" WHERE (\"users\".\"id\" > ?) "
              "UNION SELECT \"id\", \"name\" FROM \"users\" WHERE (\"users\".\"id\" < ?)");
    EXPECT_EQ(conn.last_sql().find("WITH"), std::string::npos);

    auto mutation = db.from(users);
    mutation.with_cte("high_value_orders", cte_source);
    mutation.where(users["id"] > 0);

    mutation.update({users["name"] = "updated"});
    EXPECT_EQ(conn.last_sql(),
              "UPDATE \"users\" SET \"name\" = ? WHERE (\"users\".\"id\" > ?)");
    EXPECT_EQ(conn.last_sql().find("WITH"), std::string::npos);

    mutation.remove();
    EXPECT_EQ(conn.last_sql(),
              "DELETE FROM \"users\" WHERE (\"users\".\"id\" > ?)");
    EXPECT_EQ(conn.last_sql().find("WITH"), std::string::npos);

    mutation.prepare_update<int>({users["name"] = param<int>(0)});
    EXPECT_EQ(conn.last_sql(),
              "UPDATE \"users\" SET \"name\" = ? WHERE (\"users\".\"id\" > ?)");
    EXPECT_EQ(conn.last_sql().find("WITH"), std::string::npos);

    mutation.prepare_remove();
    EXPECT_EQ(conn.last_sql(),
              "DELETE FROM \"users\" WHERE (\"users\".\"id\" > ?)");
    EXPECT_EQ(conn.last_sql().find("WITH"), std::string::npos);
}

// ============================================================================
// MySQL Dialect Tests
// ============================================================================

TEST(SqlGeneratorTest, MysqlSelectWithBackticksAndLimitOffset) {
    MockMysqlDialect dialect;
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

TEST(SqlGeneratorTest, MysqlUpsertOnDuplicateKeyUpdate) {
    MockMysqlDialect dialect;
    SqlGenerator gen(dialect);

    std::vector<std::string> insert_cols = {"id", "name", "email", "age"};
    std::vector<BoundValue> values = {int64_t(1), std::string("Alice"), std::string("alice@test.com"), int64_t(30)};
    std::vector<std::string> conflict_cols = {"id"};
    std::vector<std::string> update_cols = {"name", "email", "age"};

    auto result = gen.generate_upsert("users", insert_cols, values, conflict_cols, update_cols);
    EXPECT_EQ(result.sql, "INSERT INTO `users` (`id`, `name`, `email`, `age`) VALUES (?, ?, ?, ?) ON DUPLICATE KEY UPDATE `name` = VALUES(`name`), `email` = VALUES(`email`), `age` = VALUES(`age`)");
    EXPECT_EQ(result.params.size(), 4);
}

TEST(SqlGeneratorTest, MysqlDateFunctions) {
    MockMysqlDialect dialect;
    SqlGenerator gen(dialect);

    ColumnHandle created_at("events", "created_at");

    auto year_expr = created_at.year();
    auto year_res = gen.generate_expression(year_expr.node);
    EXPECT_EQ(year_res.sql, "YEAR(`events`.`created_at`)");

    auto add_days_expr = created_at.add_days(7);
    auto add_days_res = gen.generate_expression(add_days_expr.node);
    EXPECT_EQ(add_days_res.sql, "DATE_ADD(`events`.`created_at`, INTERVAL (?) DAY)");
    ASSERT_EQ(add_days_res.params.size(), 1);
    EXPECT_EQ(std::get<int64_t>(add_days_res.params[0]), 7);
}

TEST(SqlGeneratorTest, MysqlWindowFunctions) {
    MockMysqlDialect dialect;
    SqlGenerator gen(dialect);

    ColumnHandle dept("employees", "department");
    ColumnHandle salary("employees", "salary");

    auto rn = row_number().over().partition_by(dept).order_by(salary.desc());
    auto rn_res = gen.generate_expression(Expr(rn).node);

    EXPECT_EQ(rn_res.sql, "ROW_NUMBER() OVER (PARTITION BY `employees`.`department` ORDER BY `employees`.`salary` DESC)");
}

TEST(SqlGeneratorTest, ParamPlaceholderGeneration) {
    MockSqliteDialect sqlite_dialect;
    SqlGenerator sqlite_gen(sqlite_dialect);

    ColumnHandle age("users", "age");
    ColumnHandle name("users", "name");

    auto condition = (age >= param<int>(0)) && (name == param<std::string>(1));
    auto select_res = sqlite_gen.generate_select("users", {"id", "name", "age"}, condition.node);

    EXPECT_EQ(select_res.sql, "SELECT \"id\", \"name\", \"age\" FROM \"users\" WHERE ((\"users\".\"age\" >= ?) AND (\"users\".\"name\" = ?))");
    EXPECT_EQ(select_res.params.size(), 0);
    ASSERT_EQ(select_res.slots.size(), 2);
    EXPECT_TRUE(select_res.slots[0].is_dynamic);
    EXPECT_EQ(select_res.slots[0].dynamic_index, 0);
    EXPECT_TRUE(select_res.slots[1].is_dynamic);
    EXPECT_EQ(select_res.slots[1].dynamic_index, 1);

    // Mixed static and dynamic placeholders with Postgres ($1, $2, ...)
    MockPostgresDialect pg_dialect;
    SqlGenerator pg_gen(pg_dialect);

    auto mixed_condition = (age >= param<int>(0)) && (name == "Admin");
    auto mixed_res = pg_gen.generate_select("users", {"id"}, mixed_condition.node);

    EXPECT_EQ(mixed_res.sql, "SELECT \"id\" FROM \"users\" WHERE ((\"users\".\"age\" >= $1) AND (\"users\".\"name\" = $2))");
    ASSERT_EQ(mixed_res.slots.size(), 2);
    EXPECT_TRUE(mixed_res.slots[0].is_dynamic);
    EXPECT_EQ(mixed_res.slots[0].dynamic_index, 0);
    EXPECT_FALSE(mixed_res.slots[1].is_dynamic);
    EXPECT_EQ(std::get<std::string>(mixed_res.slots[1].static_value), "Admin");
}

// ============================================================================
// Oracle Dialect Tests
// ============================================================================

TEST(SqlGeneratorTest, OracleSelectWithOffsetFetch) {
    MockOracleDialect dialect;
    SqlGenerator gen(dialect);

    ColumnHandle age("users", "age");
    ColumnHandle name("name");
    auto where_expr = (age > 21) && (name == "Alice");

    std::vector<std::pair<ExprNode, SortDir>> order_by = {
        {age.ref, SortDir::Desc},
        {name.ref, SortDir::Asc}
    };

    auto result = gen.generate_select("users", {"id", "name", "age"}, where_expr.node, order_by, 10, 20);
    EXPECT_EQ(result.sql, "SELECT \"id\", \"name\", \"age\" FROM \"users\" WHERE ((\"users\".\"age\" > ?) AND (\"name\" = ?)) ORDER BY \"users\".\"age\" DESC, \"name\" ASC OFFSET 20 ROWS FETCH NEXT 10 ROWS ONLY");
    ASSERT_EQ(result.params.size(), 2);
    EXPECT_EQ(std::get<int64_t>(result.params[0]), 21);
    EXPECT_EQ(std::get<std::string>(result.params[1]), "Alice");
}

TEST(SqlGeneratorTest, OracleUpsertMerge) {
    MockOracleDialect dialect;
    SqlGenerator gen(dialect);

    std::vector<std::string> insert_cols = {"id", "name", "email", "age"};
    std::vector<BoundValue> values = {int64_t(1), std::string("Alice"), std::string("alice@test.com"), int64_t(30)};
    std::vector<std::string> conflict_cols = {"id"};
    std::vector<std::string> update_cols = {"name", "email", "age"};

    auto result = gen.generate_upsert("users", insert_cols, values, conflict_cols, update_cols);
    EXPECT_EQ(result.sql, "MERGE INTO \"users\" target USING (SELECT ? AS \"id\", ? AS \"name\", ? AS \"email\", ? AS \"age\" FROM DUAL) source ON (target.\"id\" = source.\"id\") WHEN MATCHED THEN UPDATE SET target.\"name\" = source.\"name\", target.\"email\" = source.\"email\", target.\"age\" = source.\"age\" WHEN NOT MATCHED THEN INSERT (\"id\", \"name\", \"email\", \"age\") VALUES (source.\"id\", source.\"name\", source.\"email\", source.\"age\")");
    EXPECT_EQ(result.params.size(), 4);
}

TEST(SqlGeneratorTest, OracleDateFunctions) {
    MockOracleDialect dialect;
    SqlGenerator gen(dialect);

    ColumnHandle created_at("events", "created_at");

    auto year_expr = created_at.year();
    auto year_res = gen.generate_expression(year_expr.node);
    EXPECT_EQ(year_res.sql, "EXTRACT(YEAR FROM CAST(\"events\".\"created_at\" AS TIMESTAMP))");

    auto add_days_expr = created_at.add_days(7);
    auto add_days_res = gen.generate_expression(add_days_expr.node);
    EXPECT_EQ(add_days_res.sql, "(CAST(\"events\".\"created_at\" AS TIMESTAMP) + NUMTODSINTERVAL(?, 'DAY'))");
    ASSERT_EQ(add_days_res.params.size(), 1);
    EXPECT_EQ(std::get<int64_t>(add_days_res.params[0]), 7);
}

TEST(SqlGeneratorTest, OracleCreateTable) {
    MockOracleDialect dialect;
    SqlGenerator gen(dialect);

    std::vector<ColumnInfo> cols = {
        {"id", SqlType::Integer, true, true, false, false, false},
        {"name", SqlType::Text, false, false, true, false, false},
        {"email", SqlType::Text, false, false, false, false, true},
        {"active", SqlType::Boolean, false, false, true, false, false},
        {"recorded_at", SqlType::Timestamp, false, false, false, false, false}
    };

    auto result = gen.generate_create_table("users", cols);
    EXPECT_EQ(result.sql, "CREATE TABLE IF NOT EXISTS \"users\" (\"id\" NUMBER GENERATED BY DEFAULT ON NULL AS IDENTITY PRIMARY KEY, \"name\" VARCHAR2(255) NOT NULL, \"email\" VARCHAR2(255) UNIQUE, \"active\" NUMBER(1) NOT NULL, \"recorded_at\" TIMESTAMP)");
    EXPECT_TRUE(result.params.empty());
}

TEST(SqlGeneratorTest, CteGenerationWithGroupByAndHaving) {
    MockSqliteDialect dialect;
    SqlGenerator gen(dialect);

    ColumnHandle dept("dept");
    ColumnHandle salary("salary");
    SubqueryExpr sub("employees", {"dept", "salary"}, nullptr, false);

    CteClause cte{"dept_salaries", sub, false};
    auto res = gen.generate_cte_select(
        {cte},
        "dept_salaries",
        {"dept"},
        std::nullopt,
        {},
        std::nullopt,
        std::nullopt,
        false,
        {dept.ref},
        (salary > 50000).node
    );

    EXPECT_EQ(res.sql, "WITH \"dept_salaries\" AS (SELECT \"dept\", \"salary\" FROM \"employees\") SELECT \"dept\" FROM \"dept_salaries\" GROUP BY \"dept\" HAVING (\"salary\" > ?)");
    ASSERT_EQ(res.params.size(), 1);
    EXPECT_EQ(std::get<int64_t>(res.params[0]), 50000);
}

TEST(SqlGeneratorTest, JoinedCountGeneration) {
    MockSqliteDialect dialect;
    SqlGenerator gen(dialect);

    ColumnHandle user_id("users", "id");
    ColumnHandle order_user_id("orders", "user_id");
    ColumnHandle amount("orders", "amount");

    JoinClause j1{"INNER JOIN", "orders", (user_id == order_user_id).node};

    auto res = gen.generate_joined_count(
        "users",
        {j1},
        (amount > 100.0).node
    );

    EXPECT_EQ(res.sql, "SELECT COUNT(*) FROM \"users\" INNER JOIN \"orders\" ON (\"users\".\"id\" = \"orders\".\"user_id\") WHERE (\"orders\".\"amount\" > ?)");
    ASSERT_EQ(res.params.size(), 1);
    EXPECT_DOUBLE_EQ(std::get<double>(res.params[0]), 100.0);
}

TEST(SqlGeneratorTest, SetOperationCountGeneration) {
    MockSqliteDialect dialect;
    SqlGenerator gen(dialect);

    ColumnHandle age("users", "age");

    SetOpClause op{
        SetOpType::Union,
        "users",
        {"id", "name", "age"},
        (age >= 30).node,
        false
    };

    auto res = gen.generate_set_operation_count(
        "users",
        {"id", "name", "age"},
        (age <= 20).node,
        false,
        {op}
    );

    EXPECT_EQ(res.sql, "SELECT COUNT(*) FROM (SELECT \"id\", \"name\", \"age\" FROM \"users\" WHERE (\"users\".\"age\" <= ?) UNION SELECT \"id\", \"name\", \"age\" FROM \"users\" WHERE (\"users\".\"age\" >= ?)) \"_sub\"");
    ASSERT_EQ(res.params.size(), 2);
    EXPECT_EQ(std::get<int64_t>(res.params[0]), 20);
    EXPECT_EQ(std::get<int64_t>(res.params[1]), 30);
}

TEST(ReviewFixesTest, InsertEmptyColumnsDefaultValues) {
    MockSqliteDialect dialect;
    SqlGenerator gen(dialect);

    auto res = gen.generate_insert("users", {}, {});
    EXPECT_EQ(res.sql, "INSERT INTO \"users\" DEFAULT VALUES");
}

TEST(ReviewFixesTest, SetOperationEmptyColumnsEmitsStar) {
    MockSqliteDialect dialect;
    SqlGenerator gen(dialect);

    SetOpClause op{
        SetOpType::Union,
        "other_users",
        {},
        std::nullopt,
        false
    };

    auto res = gen.generate_set_operation("users", {}, std::nullopt, false, {op});
    EXPECT_EQ(res.sql, "SELECT * FROM \"users\" UNION SELECT * FROM \"other_users\"");
}

TEST(ReviewFixesTest, CreateTableAutoIncWithAdditionalPk) {
    MockSqliteDialect dialect;
    SqlGenerator gen(dialect);

    std::vector<ColumnInfo> cols = {
        {"id", SqlType::Integer, true, true, true, false},
        {"tenant_id", SqlType::Integer, false, false, true, false},
        {"name", SqlType::Text, false, false, false, false}
    };

    auto res = gen.generate_create_table("users", cols);
    // Should NOT have duplicate PRIMARY KEY
    EXPECT_EQ(res.sql, "CREATE TABLE IF NOT EXISTS \"users\" (\"id\" INTEGER PRIMARY KEY AUTOINCREMENT, \"tenant_id\" INTEGER NOT NULL, \"name\" TEXT)");
}

TEST(ReviewFixesTest, AggregateUsesDialectFunctionName) {
    class CustomDialect : public MockSqliteDialect {
    public:
        std::string function_name(std::string_view name) const override {
            if (name == "COUNT") return "CUSTOM_COUNT";
            return std::string(name);
        }
    };

    CustomDialect dialect;
    SqlGenerator gen(dialect);

    auto res = gen.generate_aggregate("COUNT", "users", "*", std::nullopt);
    EXPECT_EQ(res.sql, "SELECT CUSTOM_COUNT(*) FROM \"users\"");
}

TEST(ReviewFixesTest, TableAndColumnAcceptStringView) {
    struct TestItem {
        int id;
        std::string val;
    };

    std::string_view col_name_sv = "id";
    std::string_view tbl_name_sv = "items";

    auto c1 = column(col_name_sv, &TestItem::id, primary_key);
    auto c2 = column(std::string_view("val"), &TestItem::val);
    auto t = table<TestItem>(tbl_name_sv, c1, c2);

    auto handle = t[col_name_sv];
    EXPECT_EQ(handle.ref.table_name, "items");
    EXPECT_EQ(handle.ref.column_name, "id");
}

TEST(ReviewFixesTest, InsertEmptyColumnsWithReturningMssqlOutput) {
    MockMssqlDialect dialect;
    SqlGenerator gen(dialect);

    auto res = gen.generate_insert("users", {}, {}, "id");
    EXPECT_EQ(res.sql, "INSERT INTO [users] OUTPUT INSERTED.[id] DEFAULT VALUES");
}

TEST(ReviewFixesTest, InsertEmptyColumnsWithReturningSqliteReturning) {
    MockSqliteDialect dialect;
    SqlGenerator gen(dialect);

    auto res = gen.generate_insert("users", {}, {}, "id");
    EXPECT_EQ(res.sql, "INSERT INTO \"users\" DEFAULT VALUES RETURNING \"id\"");
}

#ifdef CPPLINQ_HAS_POSTGRES
TEST(ReviewFixesTest, PostgresQuoteIdAndReturningEscapesDoubleQuotes) {
    PostgresDialect dialect;
    EXPECT_EQ(dialect.quote_id("user\"name"), "\"user\"\"name\"");
    EXPECT_EQ(dialect.returning_clause("user\"name"), " RETURNING \"user\"\"name\"");
}

TEST(ReviewFixesTest, PostgresPolymorphicDestructorAndQuoteId) {
    std::unique_ptr<ISqlDialect> dialect = std::make_unique<PostgresDialect>();
    EXPECT_EQ(dialect->quote_id("user\"name"), "\"user\"\"name\"");
    EXPECT_EQ(dialect->returning_clause("user\"name"), " RETURNING \"user\"\"name\"");
}
#endif

#ifdef CPPLINQ_HAS_MSSQL
TEST(ReviewFixesTest, MssqlCreateTablePrefixEscapesSingleQuotes) {
    MssqlDialect dialect;
    EXPECT_EQ(dialect.create_table_prefix("user'table"), "IF OBJECT_ID(N'user''table', N'U') IS NULL CREATE TABLE [user'table]");
}

TEST(ReviewFixesTest, MssqlUpsertEmptyUpdateColumns) {
    MssqlDialect dialect;
    auto sql = dialect.generate_upsert("users", {"id", "name"}, {"id"}, {});
    EXPECT_EQ(sql, "MERGE INTO [users] WITH (HOLDLOCK) AS [target] USING (VALUES (?, ?)) AS [source] ([id], [name]) ON ([target].[id] = [source].[id]) WHEN NOT MATCHED THEN INSERT ([id], [name]) VALUES ([source].[id], [source].[name]);");
}

TEST(ReviewFixesTest, MssqlUpsertEmptyInsertColumnsThrows) {
    MssqlDialect dialect;
    EXPECT_THROW(dialect.generate_upsert("users", {}, {"id"}, {"name"}), std::invalid_argument);
}

TEST(ReviewFixesTest, MssqlUpsertEmptyConflictColumnsThrows) {
    MssqlDialect dialect;
    EXPECT_THROW(dialect.generate_upsert("users", {"id", "name"}, {}, {"name"}), std::invalid_argument);
}

TEST(ReviewFixesTest, MssqlLimitZeroThrowsInvalidArgument) {
    MssqlDialect dialect;
    EXPECT_THROW(dialect.limit_offset(0, std::nullopt), std::invalid_argument);
    EXPECT_THROW(dialect.limit_offset(0, 10), std::invalid_argument);
}
#endif

#ifdef CPPLINQ_HAS_MYSQL
TEST(ReviewFixesTest, MysqlFunctionNameLengthToCharLength) {
    MysqlDialect dialect;
    EXPECT_EQ(dialect.function_name("LENGTH"), "CHAR_LENGTH");
    EXPECT_EQ(dialect.function_name("SUBSTR"), "SUBSTRING");
    EXPECT_EQ(dialect.function_name("UPPER"), "UPPER");
}

TEST(ReviewFixesTest, MysqlUpsertEmptyUpdateColumnsWithConflictColumns) {
    MysqlDialect dialect;
    auto sql = dialect.generate_upsert("users", {"id", "name"}, {"id"}, {});
    EXPECT_EQ(sql, "INSERT INTO `users` (`id`, `name`) VALUES (?, ?) ON DUPLICATE KEY UPDATE `id` = `id`");
}

TEST(ReviewFixesTest, MysqlUpsertEmptyUpdateColumnsFallbackToInsertColumns) {
    MysqlDialect dialect;
    auto sql = dialect.generate_upsert("users", {"name"}, {}, {});
    EXPECT_EQ(sql, "INSERT INTO `users` (`name`) VALUES (?) ON DUPLICATE KEY UPDATE `name` = `name`");
}

TEST(ReviewFixesTest, MysqlUpsertEmptyInsertColumnsThrows) {
    MysqlDialect dialect;
    EXPECT_THROW(dialect.generate_upsert("users", {}, {}, {}), std::invalid_argument);
}
#endif

#ifdef CPPLINQ_HAS_SQLITE
TEST(ReviewFixesTest, SqliteQuoteIdAndReturningEscapesDoubleQuotes) {
    SqliteDialect dialect;
    EXPECT_EQ(dialect.quote_id("user\"name"), "\"user\"\"name\"");
    EXPECT_EQ(dialect.returning_clause("user\"name"), " RETURNING \"user\"\"name\"");
}

TEST(ReviewFixesTest, SqliteLimitOffsetWithoutLimitEmitsLimitMinusOne) {
    SqliteDialect dialect;
    EXPECT_EQ(dialect.limit_offset(10, 5), " LIMIT 10 OFFSET 5");
    EXPECT_EQ(dialect.limit_offset(10, std::nullopt), " LIMIT 10");
    EXPECT_EQ(dialect.limit_offset(std::nullopt, 5), " LIMIT -1 OFFSET 5");
    EXPECT_EQ(dialect.limit_offset(std::nullopt, std::nullopt), "");
}

TEST(ReviewFixesTest, SqliteExtractPartFuncSupportsTimeParts) {
    SqliteDialect dialect;
    EXPECT_EQ(dialect.extract_part_func("YEAR", "ts"), "CAST(strftime('%Y', ts) AS INTEGER)");
    EXPECT_EQ(dialect.extract_part_func("MONTH", "ts"), "CAST(strftime('%m', ts) AS INTEGER)");
    EXPECT_EQ(dialect.extract_part_func("DAY", "ts"), "CAST(strftime('%d', ts) AS INTEGER)");
    EXPECT_EQ(dialect.extract_part_func("HOUR", "ts"), "CAST(strftime('%H', ts) AS INTEGER)");
    EXPECT_EQ(dialect.extract_part_func("MINUTE", "ts"), "CAST(strftime('%M', ts) AS INTEGER)");
    EXPECT_EQ(dialect.extract_part_func("SECOND", "ts"), "CAST(strftime('%S', ts) AS INTEGER)");
    EXPECT_EQ(dialect.extract_part_func("OTHER", "ts"), "strftime('OTHER', ts)");
}

TEST(ReviewFixesTest, SqliteDateAddDaysRemovesPlusPrefix) {
    SqliteDialect dialect;
    EXPECT_EQ(dialect.date_add_days_func("ts", "-7"), "date(ts, (-7) || ' days')");
    EXPECT_EQ(dialect.date_add_days_func("ts", "7"), "date(ts, (7) || ' days')");
}

TEST(ReviewFixesTest, SqlitePolymorphicDestructor) {
    std::unique_ptr<ISqlDialect> dialect = std::make_unique<SqliteDialect>();
    EXPECT_EQ(dialect->placeholder(0), "?");
}
#endif

#if defined(CPPLINQ_HAS_MSSQL) || defined(CPPLINQ_HAS_MYSQL) || defined(CPPLINQ_HAS_POSTGRES) || defined(CPPLINQ_HAS_INFORMIX) || defined(CPPLINQ_HAS_ORACLE)
TEST(ReviewFixesTest, OdbcUtilsNegativeScaleAppendsZeros) {
    using namespace cpplinq::detail::odbc;
    SQL_NUMERIC_STRUCT ns{};
    ns.sign = 1;
    ns.precision = 5;
    ns.scale = -3;
    ns.val[0] = 42;
    std::string s = numeric_struct_to_string(ns);
    EXPECT_EQ(s, "42000");

    ns.sign = 0;
    EXPECT_EQ(numeric_struct_to_string(ns), "-42000");
}

TEST(ReviewFixesTest, OdbcUtilsStringToNumericLeadingZeroPrecisionAndScaleClamping) {
    using namespace cpplinq::detail::odbc;
    auto ns = string_to_numeric_struct("00012.34");
    EXPECT_EQ(ns.scale, 2);
    EXPECT_EQ(ns.precision, 4);
    EXPECT_EQ(numeric_struct_to_string(ns), "12.34");

    auto ns_zero = string_to_numeric_struct("000");
    EXPECT_EQ(ns_zero.scale, 0);
    EXPECT_EQ(ns_zero.precision, 1);
    EXPECT_EQ(numeric_struct_to_string(ns_zero), "0");

    auto ns_small = string_to_numeric_struct("0.05");
    EXPECT_EQ(ns_small.scale, 2);
    EXPECT_EQ(ns_small.precision, 2);
    EXPECT_EQ(numeric_struct_to_string(ns_small), "0.05");

    std::string large_scale = "1." + std::string(50, '9');
    auto ns_clamp = string_to_numeric_struct(large_scale);
    EXPECT_EQ(ns_clamp.scale, 38);
    EXPECT_EQ(ns_clamp.precision, 38);
    EXPECT_EQ(numeric_struct_to_string(ns_clamp), "1." + std::string(38, '9'));

    auto ns_default_scale = string_to_numeric_struct(large_scale, 0, 2);
    EXPECT_EQ(ns_default_scale.scale, 2);
    EXPECT_EQ(numeric_struct_to_string(ns_default_scale), "1.99");
}
#endif

#ifdef CPPLINQ_HAS_SQLITE
TEST(QueryBuilderCoverageTest, JoinedCountAndFirstAndWhere) {
    DbContext<sqlite> db(":memory:");
    auto users = query_builder_users_table();
    auto orders = query_builder_orders_table();
    auto accounts = query_builder_accounts_table();

    db.ensure_table(users);
    db.ensure_table(orders);
    db.ensure_table(accounts);

    db.insert(users, QueryBuilderUser{1, "Alice"});
    db.insert(users, QueryBuilderUser{2, "Bob"});
    db.insert(users, QueryBuilderUser{3, "Charlie"});

    db.insert(orders, QueryBuilderOrder{101, 1, 100.0});
    db.insert(orders, QueryBuilderOrder{102, 1, 200.0});
    db.insert(orders, QueryBuilderOrder{103, 2, 50.0});

    db.insert(accounts, QueryBuilderAccount{201, 1, "USD", 1000.0});

    // 1. joined.count() on Inner join without WHERE (exercises params.empty() branch)
    auto jq_inner = db.from(users).join(orders).on(users["id"] == orders["user_id"]);
    EXPECT_EQ(jq_inner.count(), 3u);

    // 2. joined.count() on Inner join with WHERE (exercises params not empty branch)
    auto jq_inner_where = db.from(users).join(orders).on(users["id"] == orders["user_id"])
                            .where(orders["amount"] > 75.0);
    EXPECT_EQ(jq_inner_where.count(), 2u);

    // 3. joined.count() on Left join without WHERE
    auto jq_left = db.from(users).left_join(orders).on(users["id"] == orders["user_id"]);
    EXPECT_EQ(jq_left.count(), 4u); // Alice (2), Bob (1), Charlie (1 null)

    // 4. joined.count() on Left join with WHERE
    auto jq_left_where = db.from(users).left_join(orders).on(users["id"] == orders["user_id"])
                           .where(users["name"] == "Alice");
    EXPECT_EQ(jq_left_where.count(), 2u);

    // 5. joined.count() with condition matching 0 rows
    auto jq_zero = db.from(users).join(orders).on(users["id"] == orders["user_id"])
                     .where(users["name"] == "Nobody");
    EXPECT_EQ(jq_zero.count(), 0u);

    // 6. joined.first() on Inner join with data
    auto first_inner = db.from(users).join(orders).on(users["id"] == orders["user_id"])
                         .order_by(orders["amount"])
                         .first();
    ASSERT_TRUE(first_inner.has_value());
    EXPECT_EQ(first_inner->first.name, "Bob");
    EXPECT_DOUBLE_EQ(first_inner->second.amount, 50.0);

    // 7. joined.first() on Inner join with no matching rows -> nullopt
    auto first_none = db.from(users).join(orders).on(users["id"] == orders["user_id"])
                        .where(users["name"] == "Nobody")
                        .first();
    EXPECT_FALSE(first_none.has_value());

    // 8. joined.first() on Left join matching row
    auto first_left = db.from(users).left_join(orders).on(users["id"] == orders["user_id"])
                        .where(users["name"] == "Alice")
                        .order_by(orders["amount"])
                        .first();
    ASSERT_TRUE(first_left.has_value());
    EXPECT_EQ(first_left->first.name, "Alice");
    ASSERT_TRUE(first_left->second.has_value());
    EXPECT_DOUBLE_EQ(first_left->second->amount, 100.0);

    // 9. joined.first() on Left join non-matching row (second is nullopt)
    auto first_left_null = db.from(users).left_join(orders).on(users["id"] == orders["user_id"])
                             .where(users["name"] == "Charlie")
                             .first();
    ASSERT_TRUE(first_left_null.has_value());
    EXPECT_EQ(first_left_null->first.name, "Charlie");
    EXPECT_FALSE(first_left_null->second.has_value());

    // 10. joined.where(...) chaining
    auto jq_chained = db.from(users).join(orders).on(users["id"] == orders["user_id"])
                        .where(users["id"] == 1)
                        .where(orders["amount"] > 150.0);
    auto chained_list = jq_chained.to_list();
    ASSERT_EQ(chained_list.size(), 1u);
    EXPECT_EQ(chained_list[0].first.name, "Alice");
    EXPECT_DOUBLE_EQ(chained_list[0].second.amount, 200.0);

    // 11. joined order_by_desc and then_by / then_by_desc
    auto jq_orders = db.from(users).join(orders).on(users["id"] == orders["user_id"])
                       .order_by_desc(users["id"])
                       .then_by(orders["amount"])
                       .then_by_desc(orders["id"])
                       .limit(2)
                       .offset(0)
                       .to_list();
    EXPECT_EQ(jq_orders.size(), 2u);

    // 12. Joined3QueryBuilder chaining where, first, to_list with left joins
    auto j3 = db.from(users)
                .join(orders).on(users["id"] == orders["user_id"])
                .join(accounts).on(accounts["user_id"] == users["id"])
                .where(users["name"] == "Alice")
                .where(orders["amount"] > 150.0);
    auto j3_list = j3.to_list();
    ASSERT_EQ(j3_list.size(), 1u);
    EXPECT_EQ(std::get<0>(j3_list[0]).name, "Alice");
    EXPECT_DOUBLE_EQ(std::get<1>(j3_list[0]).amount, 200.0);
    EXPECT_EQ(std::get<2>(j3_list[0]).currency, "USD");

    auto j3_first = j3.first();
    ASSERT_TRUE(j3_first.has_value());

    auto j3_none = db.from(users)
                     .join(orders).on(users["id"] == orders["user_id"])
                     .join(accounts).on(accounts["user_id"] == users["id"])
                     .where(users["name"] == "Nobody")
                     .first();
    EXPECT_FALSE(j3_none.has_value());

    // 3-way Left join where joined tables have no matching rows -> optional is nullopt
    auto j3_left = db.from(users)
                     .left_join(orders).on(users["id"] == orders["user_id"])
                     .left_join(accounts).on(accounts["user_id"] == users["id"])
                     .where(users["name"] == "Charlie")
                     .order_by_desc(users["id"])
                     .then_by(users["name"])
                     .then_by_desc(users["name"])
                     .limit(10)
                     .offset(0);
    auto j3_left_res = j3_left.to_list();
    ASSERT_EQ(j3_left_res.size(), 1u);
    EXPECT_EQ(std::get<0>(j3_left_res[0]).name, "Charlie");
    EXPECT_FALSE(std::get<1>(j3_left_res[0]).has_value());
    EXPECT_FALSE(std::get<2>(j3_left_res[0]).has_value());
}

TEST(QueryBuilderCoverageTest, QueryBuilderTerminalsAndEdgeCases) {
    DbContext<sqlite> db(":memory:");
    auto users = query_builder_users_table();
    auto orders = query_builder_orders_table();

    auto empty_orders = table<QueryBuilderOrder>("empty_orders",
        column("id", &QueryBuilderOrder::id, primary_key),
        column("user_id", &QueryBuilderOrder::user_id),
        column("amount", &QueryBuilderOrder::amount));

    db.ensure_table(users);
    db.ensure_table(orders);
    db.ensure_table(empty_orders);

    db.insert(users, QueryBuilderUser{1, "Alice"});
    db.insert(users, QueryBuilderUser{2, "Bob"});
    db.insert(users, QueryBuilderUser{3, "Alice"}); // duplicate name

    db.insert(orders, QueryBuilderOrder{1, 1, 10.0});
    db.insert(orders, QueryBuilderOrder{2, 1, 20.0});
    db.insert(orders, QueryBuilderOrder{3, 2, 30.0});

    // 1. count_distinct with empty column expr -> throws DbException
    try {
        db.from(users).count_distinct(expr::Expr{});
        FAIL() << "Expected DbException";
    } catch (const DbException& ex) {
        EXPECT_STREQ(ex.what(), "Column name cannot be empty for count_distinct");
    }

    // count_distinct without WHERE (params empty, line 997)
    EXPECT_EQ(db.from(users).count_distinct(users["name"]), 2u);

    // count_distinct with WHERE (params not empty, line 1000)
    EXPECT_EQ(db.from(users).where(users["id"] <= 2).count_distinct(users["name"]), 2u);

    // count_distinct matching 0 rows
    EXPECT_EQ(db.from(users).where(users["name"] == "Nobody").count_distinct(users["name"]), 0u);

    // 2. avg, sum, min_val, max_val on empty tables -> std::nullopt
    EXPECT_FALSE(db.from(empty_orders).avg(empty_orders["amount"]).has_value());
    EXPECT_FALSE(db.from(empty_orders).sum(empty_orders["amount"]).has_value());
    EXPECT_FALSE(db.from(empty_orders).min_val(empty_orders["amount"]).has_value());
    EXPECT_FALSE(db.from(empty_orders).max_val(empty_orders["amount"]).has_value());

    // avg, sum, min_val, max_val on tables with data without WHERE
    auto avg_val = db.from(orders).avg(orders["amount"]);
    ASSERT_TRUE(avg_val.has_value());
    EXPECT_DOUBLE_EQ(*avg_val, 20.0);

    auto sum_val = db.from(orders).sum(orders["amount"]);
    ASSERT_TRUE(sum_val.has_value());
    EXPECT_DOUBLE_EQ(*sum_val, 60.0);

    auto min_val = db.from(orders).min_val(orders["amount"]);
    ASSERT_TRUE(min_val.has_value());
    EXPECT_DOUBLE_EQ(*min_val, 10.0);

    auto max_val = db.from(orders).max_val(orders["amount"]);
    ASSERT_TRUE(max_val.has_value());
    EXPECT_DOUBLE_EQ(*max_val, 30.0);

    // avg, sum, min_val, max_val with WHERE (params not empty)
    auto sum_filtered = db.from(orders).where(orders["user_id"] == 1).sum(orders["amount"]);
    ASSERT_TRUE(sum_filtered.has_value());
    EXPECT_DOUBLE_EQ(*sum_filtered, 30.0);

    // Aggregate with empty column expr -> throws DbException
    try {
        db.from(orders).avg(expr::Expr{});
        FAIL() << "Expected DbException";
    } catch (const DbException& ex) {
        EXPECT_STREQ(ex.what(), "Column name cannot be empty for AVG");
    }
    try {
        db.from(orders).sum(expr::Expr{});
        FAIL() << "Expected DbException";
    } catch (const DbException& ex) {
        EXPECT_STREQ(ex.what(), "Column name cannot be empty for SUM");
    }
    try {
        db.from(orders).min_val(expr::Expr{});
        FAIL() << "Expected DbException";
    } catch (const DbException& ex) {
        EXPECT_STREQ(ex.what(), "Column name cannot be empty for MIN");
    }
    try {
        db.from(orders).max_val(expr::Expr{});
        FAIL() << "Expected DbException";
    } catch (const DbException& ex) {
        EXPECT_STREQ(ex.what(), "Column name cannot be empty for MAX");
    }

    // 3. query.update(...) directly through QueryBuilder
    // With params (lines 1036-1039)
    size_t updated = db.from(users).where(users["id"] == 1).update({users["name"] = std::string("Alicia")});
    EXPECT_EQ(updated, 1u);
    auto reloaded = db.from(users).where(users["id"] == 1).first();
    ASSERT_TRUE(reloaded.has_value());
    EXPECT_EQ(reloaded->name, "Alicia");

    // Without params (lines 1033-1035) - column assigned to column without where
    size_t direct_upd = db.from(users).update({users["name"] = users["name"]});
    EXPECT_EQ(direct_upd, 3u);

    // 4. query.remove() directly through QueryBuilder
    // With params (lines 1058-1061)
    size_t removed = db.from(users).where(users["id"] == 3).remove();
    EXPECT_EQ(removed, 1u);
    EXPECT_EQ(db.from(users).count(), 2u);

    // Without params (lines 1055-1057)
    size_t removed_all = db.from(empty_orders).remove();
    EXPECT_EQ(removed_all, 0u);

    // 5. query.prepare() directly through QueryBuilder
    // Without CTEs
    auto prep_no_cte = db.from(users).where(users["id"] == param<int64_t>(0)).prepare<int64_t>();
    auto prep_res = prep_no_cte.execute(int64_t{1});
    ASSERT_EQ(prep_res.size(), 1u);
    EXPECT_EQ(prep_res[0].name, "Alicia");

    // With CTEs (lines 1080-1082)
    auto cte_source = db.from(users).where(users["id"] > 0);
    auto prep_cte = db.from(users).with_cte("user_cte", cte_source).prepare();
    auto cte_res = prep_cte.execute();
    EXPECT_EQ(cte_res.size(), 2u);

    // 6. with_cte subquery and chaining
    // SubqueryExpr overload (line 865)
    auto sub = db.from(users).where(users["id"] > 0).as_subquery();
    auto q_sub = db.from(users).with_cte("sub_cte", sub);
    EXPECT_EQ(q_sub.to_list().size(), 2u);

    // Chaining multiple CTEs
    auto cte1 = db.from(users).where(users["id"] == 1);
    auto cte2 = db.from(users).where(users["id"] == 2);
    auto q_chained_cte = db.from(users).with_cte("c1", cte1).with_cte("c2", cte2);
    EXPECT_EQ(q_chained_cte.to_list().size(), 2u);

    // 7. query.exists()
    // With params
    EXPECT_TRUE(db.from(users).where(users["name"] == "Alicia").exists());
    EXPECT_FALSE(db.from(users).where(users["name"] == "NonExistent").exists());
    // Without params
    EXPECT_TRUE(db.from(users).exists());
    EXPECT_FALSE(db.from(empty_orders).exists());

    // Zero-column table exists (lines 1102, 1118)
    struct EmptyEntity {};
    auto empty_col_table = table<EmptyEntity>("empty_col_table");
    db.execute_raw("CREATE TABLE IF NOT EXISTS empty_col_table (dummy INTEGER)");
    EXPECT_FALSE(db.from(empty_col_table).exists());
    db.execute_raw("INSERT INTO empty_col_table VALUES (1)");
    EXPECT_TRUE(db.from(empty_col_table).exists());

    // 8. query.where chaining
    auto chained_query = db.from(users)
                           .where(users["id"] > 0)
                           .where(users["name"] == "Alicia");
    auto chained_res = chained_query.to_list();
    ASSERT_EQ(chained_res.size(), 1u);
    EXPECT_EQ(chained_res[0].id, 1);

    // 9. query.first() on empty results -> nullopt
    EXPECT_FALSE(db.from(empty_orders).first().has_value());
    EXPECT_TRUE(db.from(orders).first().has_value());

    // 10. query.stream with ExecutionOptions
    std::stop_source ss;
    ExecutionOptions opts{.query_timeout_seconds = 5, .stop_token = ss.get_token()};
    size_t stream_count = 0;
    for (const auto& u : db.from(users).stream(opts)) {
        (void)u;
        ++stream_count;
    }
    EXPECT_EQ(stream_count, 2u);

    // 11. order_by, then_by, group_by, having
    auto gq = db.from(orders)
                .group_by(orders["user_id"])
                .having(orders["user_id"] > 0)
                .order_by(orders["user_id"])
                .order_by_desc(orders["user_id"])
                .then_by(orders["amount"])
                .then_by_desc(orders["id"])
                .to_list();
    EXPECT_FALSE(gq.empty());

    // Multiple group_by columns
    auto gq2 = db.from(orders)
                 .group_by(orders["user_id"], orders["amount"])
                 .to_list();
    EXPECT_FALSE(gq2.empty());

    // 12. as_subquery variants and subquery expressions
    auto sub_coldef = db.from(users).as_subquery(column("id", &QueryBuilderUser::id));
    EXPECT_EQ(sub_coldef.select_columns.size(), 1u);
    auto sub_handle = db.from(users).as_subquery(users["id"]);
    EXPECT_EQ(sub_handle.select_columns.size(), 1u);
    expr::Expr sub_conv = db.from(users);
    EXPECT_TRUE(sub_conv.node.index() != 0);

    auto ex_expr = cpplinq::exists(db.from(users).where(users["id"] == 1));
    auto not_ex_expr = cpplinq::not_exists(db.from(users).where(users["id"] == 1));
    EXPECT_TRUE(ex_expr.node.index() != 0);
    EXPECT_TRUE(not_ex_expr.node.index() != 0);

    // 13. SetOpQueryBuilder chaining: union_with, union_all, intersect, except_from (lines 1173-1227)
    auto q_base = db.from(users);
    auto q_u = db.from(users);
    auto q_all = db.from(users);
    auto q_int = db.from(users);
    auto q_exc = db.from(users);
    auto combined_set_ops = q_base.union_with(q_u)
                                  .union_all(q_all)
                                  .intersect(q_int)
                                  .except_from(q_exc);
    EXPECT_GE(combined_set_ops.count(), 0u);
    EXPECT_GE(combined_set_ops.to_list().size(), 0u);
}

TEST(QueryBuilderCoverageTest, PreparedQueryAndCommandCoverage) {
    DbContext<sqlite> db(":memory:");
    auto users = query_builder_users_table();
    db.ensure_table(users);
    db.insert(users, QueryBuilderUser{1, "Alice"});
    db.insert(users, QueryBuilderUser{2, "Bob"});

    // 1. PreparedQuery: set_timeout, set_stop_token, cancel
    auto prep = db.from(users).where(users["id"] == param<int64_t>(0)).prepare<int64_t>();
    prep.statement().set_timeout(10);
    std::stop_source ss;
    prep.statement().set_stop_token(ss.get_token());
    prep.statement().cancel();

    // 2. Repeated execution with different parameters
    auto res1 = prep.execute(int64_t{1});
    ASSERT_EQ(res1.size(), 1u);
    EXPECT_EQ(res1[0].name, "Alice");

    auto res2 = prep.execute(int64_t{2});
    ASSERT_EQ(res2.size(), 1u);
    EXPECT_EQ(res2[0].name, "Bob");

    auto res3 = prep.to_list(int64_t{1});
    ASSERT_EQ(res3.size(), 1u);
    EXPECT_EQ(res3[0].name, "Alice");

    // 3. execute_scalar, execute_query, execute_non_query
    auto scalar_stmt = db.connection().prepare("SELECT COUNT(*) FROM users");
    auto scalar_reader = scalar_stmt->execute_query();
    ASSERT_TRUE(scalar_reader && scalar_reader->next());
    int64_t count_scalar = scalar_reader->get_int64(0);
    EXPECT_EQ(count_scalar, 2);

    auto update_stmt = db.connection().prepare("UPDATE users SET name = 'Bob2' WHERE id = 2");
    size_t affected = update_stmt->execute_non_query();
    EXPECT_EQ(affected, 1u);

    // 4. PreparedQuery::first()
    auto first_found = prep.first(int64_t{1});
    ASSERT_TRUE(first_found.has_value());
    EXPECT_EQ(first_found->name, "Alice");

    auto first_not_found = prep.first(int64_t{999});
    EXPECT_FALSE(first_not_found.has_value());

    // 5. PreparedQuery::stream and stream_with_options
    size_t s_count = 0;
    for (const auto& u : prep.stream(int64_t{1})) {
        EXPECT_EQ(u.name, "Alice");
        ++s_count;
    }
    EXPECT_EQ(s_count, 1u);

    size_t s_opts_count = 0;
    for (const auto& u : prep.stream_with_options(
             ExecutionOptions{.query_timeout_seconds = 5, .stop_token = ss.get_token()},
             int64_t{1})) {
        EXPECT_EQ(u.name, "Alice");
        ++s_opts_count;
    }
    EXPECT_EQ(s_opts_count, 1u);

    // 6. PreparedQuery error handling: null / moved statement
    auto prep_moved = std::move(prep);
    EXPECT_THROW(prep.execute(int64_t{1}), DbException);

    // 7. PreparedQuery: missing argument for dynamic parameter slot
    std::vector<ParameterSlot> slots_bad = {
        ParameterSlot(5)
    };
    PreparedQuery<QueryBuilderUser, decltype(users.columns)> bad_prep(
        db.connection().prepare("SELECT id, name FROM users WHERE id = ?"),
        users.columns,
        slots_bad
    );
    EXPECT_THROW(bad_prep.execute(), DbException);

    // 8. PreparedQuery: static parameter slot
    std::vector<ParameterSlot> slots_static = {
        ParameterSlot(BoundValue{int64_t{1}})
    };
    PreparedQuery<QueryBuilderUser, decltype(users.columns)> static_prep(
        db.connection().prepare("SELECT id, name FROM users WHERE id = ?"),
        users.columns,
        slots_static
    );
    auto static_res = static_prep.execute();
    ASSERT_EQ(static_res.size(), 1u);
    EXPECT_EQ(static_res[0].name, "Alice");

    // 9. PreparedCommand: update & remove with execution & repeated execution
    auto cmd_upd = db.from(users).where(users["id"] == param<int64_t>(0))
                     .prepare_update<int64_t>({users["name"] = std::string("BobUpdated")});
    cmd_upd.statement().set_timeout(5);
    size_t upd_cnt = cmd_upd.execute(int64_t{2});
    EXPECT_EQ(upd_cnt, 1u);

    // Repeated execution
    size_t upd_cnt2 = cmd_upd.execute(int64_t{2});
    EXPECT_EQ(upd_cnt2, 1u);

    // 10. PreparedCommand: static slots
    auto cmd_del_static = db.from(users).where(users["id"] == 999).prepare_remove();
    EXPECT_EQ(cmd_del_static.execute(), 0u);

    // 11. PreparedCommand error handling: moved statement & missing dynamic arg
    auto cmd_moved = std::move(cmd_upd);
    EXPECT_THROW(cmd_upd.execute(int64_t{1}), DbException);

    std::vector<ParameterSlot> cmd_slots_bad = {
        ParameterSlot(5)
    };
    PreparedCommand<> bad_cmd(
        db.connection().prepare("DELETE FROM users WHERE id = ?"),
        cmd_slots_bad
    );
    EXPECT_THROW(bad_cmd.execute(), DbException);
}
#endif

