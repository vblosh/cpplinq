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

TEST(InformixQueryBuilderTest, UpsertWithoutUpdateColumns) {
    InformixDialect dialect;
    SqlGenerator gen(dialect);

    std::vector<std::string> insert_cols = {"id", "name"};
    std::vector<BoundValue> values = {int64_t(1), std::string("Alice")};
    std::vector<std::string> conflict_cols = {"id"};
    std::vector<std::string> update_cols = {};

    auto result = gen.generate_upsert("users", insert_cols, values, conflict_cols, update_cols);
    EXPECT_EQ(result.sql, "MERGE INTO \"users\" AS target USING (SELECT CAST(? AS BIGINT) AS \"id\", CAST(? AS VARCHAR(255)) AS \"name\" FROM \"informix\".systables WHERE tabid = 1) AS source ON (target.\"id\" = source.\"id\") WHEN NOT MATCHED THEN INSERT (\"id\", \"name\") VALUES (source.\"id\", source.\"name\")");
    EXPECT_EQ(result.params.size(), 2);
}

TEST(InformixQueryBuilderTest, UpsertCastTypesUint64BlobAndLvarchar) {
    InformixDialect dialect;
    SqlGenerator gen(dialect);

    std::string long_str(300, 'x');
    std::wstring long_wstr(300, L'y');
    std::vector<std::string> insert_cols = {"u64_col", "blob_col", "long_str_col", "short_str_col", "long_wstr_col", "short_wstr_col"};
    std::vector<BoundValue> values = {
        uint64_t(123456789),
        std::vector<uint8_t>{1, 2, 3, 4},
        long_str,
        std::string("short"),
        long_wstr,
        std::wstring(L"short")
    };
    std::vector<std::string> conflict_cols = {"u64_col"};
    std::vector<std::string> update_cols = {"blob_col", "long_str_col"};

    auto result = gen.generate_upsert("items", insert_cols, values, conflict_cols, update_cols);
    EXPECT_EQ(result.sql, "MERGE INTO \"items\" AS target USING (SELECT CAST(? AS DECIMAL(20, 0)) AS \"u64_col\", CAST(? AS BLOB) AS \"blob_col\", CAST(? AS LVARCHAR(300)) AS \"long_str_col\", CAST(? AS VARCHAR(255)) AS \"short_str_col\", CAST(? AS LVARCHAR(300)) AS \"long_wstr_col\", CAST(? AS NVARCHAR(255)) AS \"short_wstr_col\" FROM \"informix\".systables WHERE tabid = 1) AS source ON (target.\"u64_col\" = source.\"u64_col\") WHEN MATCHED THEN UPDATE SET target.\"blob_col\" = source.\"blob_col\", target.\"long_str_col\" = source.\"long_str_col\" WHEN NOT MATCHED THEN INSERT (\"u64_col\", \"blob_col\", \"long_str_col\", \"short_str_col\", \"long_wstr_col\", \"short_wstr_col\") VALUES (source.\"u64_col\", source.\"blob_col\", source.\"long_str_col\", source.\"short_str_col\", source.\"long_wstr_col\", source.\"short_wstr_col\")");
    EXPECT_EQ(result.params.size(), 6);
}

TEST(InformixQueryBuilderTest, FunctionName) {
    InformixDialect dialect;
    EXPECT_EQ(dialect.function_name("SUBSTR"), "SUBSTR");
    EXPECT_EQ(dialect.function_name("UPPER"), "UPPER");
    EXPECT_EQ(dialect.function_name("LOWER"), "LOWER");
}

TEST(InformixQueryBuilderTest, DialectPrimitivesAndPaginationForms) {
    InformixDialect dialect;

    EXPECT_EQ(dialect.quote_id("users"), "\"users\"");
    EXPECT_EQ(dialect.quote_id("col\"name"), "\"col\"\"name\"");
    EXPECT_EQ(dialect.quote_id("schema.table"), "\"schema.table\"");
    EXPECT_EQ(dialect.quote_id(""), "\"\"");
    EXPECT_EQ(dialect.create_table_prefix("a\"b"), "CREATE TABLE IF NOT EXISTS \"a\"\"b\"");

    EXPECT_EQ(dialect.placeholder(0), "?");
    EXPECT_EQ(dialect.placeholder(42), "?");

    // Informix places pagination before the select list, so the trailing clause is empty.
    EXPECT_EQ(dialect.limit_offset(10, 20), "");
    EXPECT_EQ(dialect.limit_offset(10, std::nullopt), "");
    EXPECT_EQ(dialect.select_prefix_limit_offset(10, 20), "SKIP 20 FIRST 10 ");
    EXPECT_EQ(dialect.select_prefix_limit_offset(std::nullopt, 20), "SKIP 20 ");
    EXPECT_EQ(dialect.select_prefix_limit_offset(10, std::nullopt), "FIRST 10 ");
    EXPECT_EQ(dialect.select_prefix_limit_offset(std::nullopt, std::nullopt), "");
}

TEST(InformixQueryBuilderTest, TypeNamesAndStatementClauses) {
    InformixDialect dialect;

    EXPECT_EQ(dialect.type_name(SqlType::Integer), "INTEGER");
    EXPECT_EQ(dialect.type_name(SqlType::BigInt), "BIGINT");
    EXPECT_EQ(dialect.type_name(SqlType::UnsignedBigInt), "DECIMAL(20, 0)");
    EXPECT_EQ(dialect.type_name(SqlType::Real), "FLOAT");
    EXPECT_EQ(dialect.type_name(SqlType::Decimal), "DECIMAL(28, 10)");
    EXPECT_EQ(dialect.type_name(SqlType::Text), "VARCHAR(255)");
    EXPECT_EQ(dialect.type_name(SqlType::WString), "NVARCHAR(255)");
    EXPECT_EQ(dialect.type_name(SqlType::Blob), "BLOB");
    EXPECT_EQ(dialect.type_name(SqlType::Boolean), "BOOLEAN");
    EXPECT_EQ(dialect.type_name(SqlType::Date), "DATE");
    EXPECT_EQ(dialect.type_name(SqlType::Time), "DATETIME HOUR TO SECOND");
    EXPECT_EQ(dialect.type_name(SqlType::Timestamp), "DATETIME YEAR TO FRACTION(3)");
    EXPECT_EQ(dialect.type_name(SqlType::Interval), "VARCHAR(100)");
    EXPECT_EQ(dialect.type_name(SqlType::Guid), "VARCHAR(36)");

    EXPECT_EQ(dialect.auto_increment_type(), "SERIAL PRIMARY KEY");

    // Informix does not support RETURNING or OUTPUT; the driver retrieves the serial value separately.
    EXPECT_EQ(dialect.returning_clause("id"), "");
    EXPECT_EQ(dialect.output_clause("id"), "");
    EXPECT_EQ(dialect.last_insert_id_query(),
              "SELECT DBINFO('sqlca.sqlerrd1') FROM \"informix\".systables WHERE tabid = 1");
    EXPECT_EQ(dialect.last_insert_id_query("users", "id"),
              "SELECT DBINFO('sqlca.sqlerrd1') FROM \"informix\".systables WHERE tabid = 1");

    EXPECT_EQ(dialect.current_timestamp_func(), "CURRENT YEAR TO FRACTION(3)");
    EXPECT_EQ(dialect.current_date_func(), "TODAY");
    EXPECT_EQ(dialect.extract_part_func("YEAR", "recorded_at"),
              "YEAR(CAST(recorded_at AS DATETIME YEAR TO SECOND))");
    EXPECT_EQ(dialect.extract_part_func("HOUR", "recorded_at"),
              "EXTRACT(HOUR FROM CAST(recorded_at AS DATETIME YEAR TO SECOND))");
    EXPECT_EQ(dialect.date_add_days_func("recorded_at", "?"),
              "(recorded_at + (?) UNITS DAY)");
}

TEST(InformixQueryBuilderTest, UpsertCastTypesForScalarAndTemporalValues) {
    InformixDialect dialect;

    std::vector<std::string> insert_cols = {
        "bool_col", "ratio", "created_on", "at_time", "seen_at",
        "amount", "duration", "uid", "missing"
    };
    std::vector<BoundValue> values = {
        true,
        double(1.5),
        SqlDate(2026, 10, 18),
        SqlTime(12, 34, 56),
        SqlTimestamp(2026, 10, 18, 12, 34, 56),
        SqlNumeric("12.34"),
        SqlInterval::from_day_second(2, 3, 4, 5),
        SqlGuid("00112233-4455-6677-8899-aabbccddeeff"),
        std::monostate{}
    };

    auto sql = dialect.generate_upsert("casts", insert_cols, values, {"uid"}, {"ratio"});
    EXPECT_EQ(sql,
        "MERGE INTO \"casts\" AS target USING (SELECT "
        "CAST(? AS BOOLEAN) AS \"bool_col\", "
        "CAST(? AS FLOAT) AS \"ratio\", "
        "CAST(? AS DATE) AS \"created_on\", "
        "CAST(? AS DATETIME HOUR TO SECOND) AS \"at_time\", "
        "CAST(? AS DATETIME YEAR TO FRACTION(3)) AS \"seen_at\", "
        "CAST(? AS DECIMAL(28, 10)) AS \"amount\", "
        "CAST(? AS VARCHAR(100)) AS \"duration\", "
        "CAST(? AS VARCHAR(36)) AS \"uid\", "
        "CAST(? AS VARCHAR(255)) AS \"missing\" "
        "FROM \"informix\".systables WHERE tabid = 1) AS source "
        "ON (target.\"uid\" = source.\"uid\") "
        "WHEN MATCHED THEN UPDATE SET target.\"ratio\" = source.\"ratio\" "
        "WHEN NOT MATCHED THEN INSERT (\"bool_col\", \"ratio\", \"created_on\", \"at_time\", "
        "\"seen_at\", \"amount\", \"duration\", \"uid\", \"missing\") "
        "VALUES (source.\"bool_col\", source.\"ratio\", source.\"created_on\", source.\"at_time\", "
        "source.\"seen_at\", source.\"amount\", source.\"duration\", source.\"uid\", source.\"missing\")");
}

TEST(InformixQueryBuilderTest, UpsertCompositeConflictKey) {
    InformixDialect dialect;

    std::vector<std::string> insert_cols = {"tenant_id", "id", "name"};
    std::vector<BoundValue> values = {int64_t(7), int64_t(42), std::string("Alice")};

    auto sql = dialect.generate_upsert("users", insert_cols, values, {"tenant_id", "id"}, {"name"});
    EXPECT_EQ(sql,
        "MERGE INTO \"users\" AS target USING (SELECT "
        "CAST(? AS BIGINT) AS \"tenant_id\", CAST(? AS BIGINT) AS \"id\", "
        "CAST(? AS VARCHAR(255)) AS \"name\" "
        "FROM \"informix\".systables WHERE tabid = 1) AS source "
        "ON (target.\"tenant_id\" = source.\"tenant_id\" AND target.\"id\" = source.\"id\") "
        "WHEN MATCHED THEN UPDATE SET target.\"name\" = source.\"name\" "
        "WHEN NOT MATCHED THEN INSERT (\"tenant_id\", \"id\", \"name\") "
        "VALUES (source.\"tenant_id\", source.\"id\", source.\"name\")");
}

#ifdef CPPLINQ_HAS_INFORMIX
#include "driver/informix_connection.h"

namespace {

class InspectableInformixConnection : public InformixConnection {
public:
    using InformixConnection::InformixConnection;

    const std::string& connection_string_for_test() const {
        return connection_string_;
    }
};

} // namespace

TEST(InformixConnectionTest, ConnectionFactoryAndMetadata) {
    auto conn = make_connection<informix>("SERVER=ol_informix1210;DATABASE=testdb");
    ASSERT_NE(conn, nullptr);
    EXPECT_NE(dynamic_cast<InformixConnection*>(conn.get()), nullptr);
    EXPECT_FALSE(conn->is_open());
    EXPECT_EQ(conn->info().dbms_name, "Informix");
    EXPECT_EQ(conn->info().driver_name, "IBM INFORMIX ODBC DRIVER (64-bit)");
    EXPECT_TRUE(conn->capabilities().window_functions);
    EXPECT_TRUE(conn->capabilities().ctes);
    EXPECT_TRUE(conn->capabilities().upsert);
    EXPECT_TRUE(conn->capabilities().cancel);
    EXPECT_EQ(conn->dialect().quote_id("col"), "\"col\"");
}

TEST(InformixConnectionTest, ConnectionFactoryDsnWithoutEquals) {
    auto conn = make_connection<informix>("my_informix_dsn");
    ASSERT_NE(conn, nullptr);
    EXPECT_EQ(conn->info().dbms_name, "Informix");
}

TEST(InformixConnectionTest, CapabilitiesAndInfoDefaultsBeforeOpen) {
    auto conn = make_connection<informix>("InformixDSN");
    ASSERT_NE(conn, nullptr);
    EXPECT_FALSE(conn->is_open());

    auto caps = conn->capabilities();
    EXPECT_TRUE(caps.cancel);
    EXPECT_TRUE(caps.streaming);
    EXPECT_TRUE(caps.query_timeout);
    EXPECT_TRUE(caps.transactions);
    EXPECT_TRUE(caps.savepoints);
    EXPECT_FALSE(caps.returning_clause);
    EXPECT_FALSE(caps.output_clause);
    EXPECT_TRUE(caps.upsert);
    EXPECT_TRUE(caps.array_batch_insert);
    EXPECT_EQ(caps.default_batch_chunk_size, 1000u);
    EXPECT_TRUE(caps.window_functions);
    EXPECT_TRUE(caps.ctes);

    auto info = conn->info();
    EXPECT_EQ(info.driver_name, "IBM INFORMIX ODBC DRIVER (64-bit)");
    EXPECT_EQ(info.dbms_name, "Informix");
    EXPECT_TRUE(info.driver_version.empty());
    EXPECT_TRUE(info.dbms_version.empty());
    EXPECT_TRUE(info.odbc_version.empty());

    try {
        conn->prepare("SELECT 1");
        FAIL() << "prepare() should reject a closed Informix connection";
    } catch (const DbException& e) {
        EXPECT_NE(std::string(e.what()).find("IBM Informix connection is not open"), std::string::npos);
    }
}

TEST(InformixConnectionTest, ConnectionStringNormalization) {
    InspectableInformixConnection full(
        "Driver={IBM INFORMIX ODBC DRIVER (64-bit)};Server=informix;Database=testdb;"
        "Host=localhost;Service=9088;Uid=informix;Pwd=in4mix");
    EXPECT_EQ(full.connection_string_for_test(),
        "Driver={IBM INFORMIX ODBC DRIVER (64-bit)};Server=informix;Database=testdb;"
        "Host=localhost;Service=9088;Uid=informix;Pwd=in4mix;"
        "DELIMIDENT=Y;CLIENT_LOCALE=en_us.utf8;REPORT_CONVERT_ERROR=0;");

    InspectableInformixConnection dsn("InformixDSN");
    EXPECT_EQ(dsn.connection_string_for_test(),
        "DSN=InformixDSN;DELIMIDENT=Y;CLIENT_LOCALE=en_us.utf8;REPORT_CONVERT_ERROR=0;");

    InspectableInformixConnection explicit_settings(
        "DSN=custom;delimident=n;client_locale=de_de.utf8;report_convert_error=1;");
    EXPECT_EQ(explicit_settings.connection_string_for_test(),
        "DSN=custom;delimident=n;client_locale=de_de.utf8;report_convert_error=1;");
}
#endif
