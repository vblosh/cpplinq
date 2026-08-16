#include <gtest/gtest.h>
#include "cpplinq/core/sql_generator.h"
#include "cpplinq/core/expression.h"

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
            case SqlType::Integer: return "INTEGER";
            case SqlType::BigInt:  return "INTEGER";
            case SqlType::Real:    return "REAL";
            case SqlType::Text:    return "TEXT";
            case SqlType::Blob:    return "BLOB";
            case SqlType::Boolean: return "INTEGER";
        }
        return "TEXT";
    }

    std::string auto_increment_type() const override {
        return "INTEGER PRIMARY KEY AUTOINCREMENT";
    }

    std::string returning_clause(std::string_view column) const override {
        return " RETURNING \"" + std::string(column) + "\"";
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
        }
        return "TEXT";
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
        }
        return "NVARCHAR(MAX)";
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

TEST(SqlGeneratorTest, UpsertMssql) {
    MockMssqlDialect dialect;
    SqlGenerator gen(dialect);

    std::vector<BoundValue> values = {int64_t{1}, std::string{"Alice"}, int64_t{30}};
    auto result = gen.generate_upsert("users", {"id", "name", "age"}, values, {"id"}, {"name", "age"});

    EXPECT_EQ(result.sql, "MERGE INTO [users] WITH (HOLDLOCK) AS [target] USING (VALUES (?, ?, ?)) AS [source] ([id], [name], [age]) ON ([target].[id] = [source].[id]) WHEN MATCHED THEN UPDATE SET [target].[name] = [source].[name], [target].[age] = [source].[age] WHEN NOT MATCHED THEN INSERT ([id], [name], [age]) VALUES ([source].[id], [source].[name], [source].[age]);");
    ASSERT_EQ(result.params.size(), 3);
}

