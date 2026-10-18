#include <gtest/gtest.h>
#include "driver/odbc_utils.h"

using namespace cpplinq;
using namespace cpplinq::detail::odbc;

#if defined(CPPLINQ_HAS_MSSQL) || defined(CPPLINQ_HAS_MYSQL) || defined(CPPLINQ_HAS_POSTGRES) || defined(CPPLINQ_HAS_INFORMIX) || defined(CPPLINQ_HAS_ORACLE)

TEST(OdbcUtilsTest, ErrorHandling) {
    std::string err = get_odbc_error(SQL_HANDLE_STMT, SQL_NULL_HANDLE);
    EXPECT_EQ(err, "Unknown ODBC error");

    EXPECT_THROW({
        check_rc(SQL_ERROR, SQL_HANDLE_STMT, SQL_NULL_HANDLE, "TestContext");
    }, DbException);
}

TEST(OdbcUtilsTest, WideStringConversions) {
    std::wstring empty_w = sqlwchar_to_wstring(nullptr, 0);
    EXPECT_TRUE(empty_w.empty());

    std::wstring ws = L"Hello, World!";
    auto sqlw = wstring_to_sqlwchar(ws);
    EXPECT_FALSE(sqlw.empty());

    std::wstring roundtrip = sqlwchar_to_wstring(sqlw.data(), sqlw.size());
    EXPECT_EQ(roundtrip, ws);

    // Surrogate pair test
    std::wstring emoji = L"\U0001F600"; // grinning face
    auto sqlw_emoji = wstring_to_sqlwchar(emoji);
    std::wstring roundtrip_emoji = sqlwchar_to_wstring(sqlw_emoji.data(), sqlw_emoji.size());
    EXPECT_FALSE(roundtrip_emoji.empty());
}

TEST(OdbcUtilsTest, NumericStructConversions) {
    SQL_NUMERIC_STRUCT ns_zero = string_to_numeric_struct("0", 1, 0);
    EXPECT_EQ(numeric_struct_to_string(ns_zero), "0");

    SQL_NUMERIC_STRUCT ns_zero_scaled = string_to_numeric_struct("0.00", 3, 2);
    EXPECT_EQ(numeric_struct_to_string(ns_zero_scaled), "0.00");

    SQL_NUMERIC_STRUCT ns_pos = string_to_numeric_struct("12345.6789", 10, 4);
    EXPECT_EQ(numeric_struct_to_string(ns_pos), "12345.6789");

    SQL_NUMERIC_STRUCT ns_neg = string_to_numeric_struct("-9876.54", 8, 2);
    EXPECT_EQ(numeric_struct_to_string(ns_neg), "-9876.54");

    SQL_NUMERIC_STRUCT ns_small = string_to_numeric_struct("0.005", 5, 3);
    EXPECT_EQ(numeric_struct_to_string(ns_small), "0.005");
}

TEST(OdbcUtilsTest, IntervalStructConversions) {
    std::vector<IntervalType> types = {
        IntervalType::Year,
        IntervalType::Month,
        IntervalType::YearToMonth,
        IntervalType::Day,
        IntervalType::Hour,
        IntervalType::Minute,
        IntervalType::Second,
        IntervalType::DayToHour,
        IntervalType::DayToMinute,
        IntervalType::DayToSecond
    };

    for (auto type : types) {
        SqlInterval iv;
        iv.type = type;
        iv.years = 3;
        iv.months = 5;
        iv.days = 7;
        iv.hours = 9;
        iv.minutes = 11;
        iv.seconds = 13;
        iv.fraction = 123456;
        iv.is_negative = false;

        auto odbc_s = interval_to_odbc_struct(iv);
        auto back_iv = odbc_struct_to_interval(odbc_s);

        EXPECT_EQ(back_iv.type, iv.type);
        EXPECT_EQ(back_iv.is_negative, iv.is_negative);

        // Negative check
        iv.is_negative = true;
        auto odbc_neg = interval_to_odbc_struct(iv);
        auto back_neg = odbc_struct_to_interval(odbc_neg);
        EXPECT_TRUE(back_neg.is_negative);
    }
}

TEST(OdbcUtilsTest, GuidStructConversions) {
    SqlGuid g("12345678-1234-5678-1234-567812345678");
    SQLGUID odbc_g = guid_to_odbc_struct(g);
    SqlGuid back_g = odbc_struct_to_guid(odbc_g);
    EXPECT_EQ(back_g, g);
}

#include <stop_token>
#include <functional>
#include <optional>
#define private public
#define protected public
#include "driver/odbc_connection.h"
#undef private
#undef protected
#include "dialect/postgres_dialect.h"

TEST(OdbcUtilsTest, BatchInsertZeroRows) {
    SQLHDBC hdbc = SQL_NULL_HDBC;
    std::vector<BoundValue> params;
    size_t rows = execute_insert_many_batch(hdbc, "INSERT INTO t VALUES (?)", params, 0, 0, "TestDriver");
    EXPECT_EQ(rows, 0);

    // flat_params smaller than row_count * col_count
    std::vector<BoundValue> short_params = {int64_t{1}};
    EXPECT_THROW({
        execute_insert_many_batch(hdbc, "INSERT INTO t VALUES (?, ?)", short_params, 2, 2, "TestDriver");
    }, DbException);
}

// ----------------------------------------------------------------------------
// connection.h default implementations tests
// ----------------------------------------------------------------------------

class DummyDataReader : public IDataReader {
public:
    bool return_null = false;

    bool next() override { return false; }
    int column_count() const override { return 1; }
    bool is_null(int) const override { return return_null; }
    int64_t get_int64(int) const override { return 42; }
    uint64_t get_uint64(int) const override { return 42; }
    double get_double(int) const override { return 42.0; }
    std::string get_string(int) const override { return "dummy_value"; }
    std::wstring get_wstring(int) const override { return L"dummy_value"; }
    bool get_bool(int) const override { return true; }
    std::vector<uint8_t> get_blob(int) const override { return {1, 2, 3}; }
    SqlNumeric get_numeric(int) const override { return SqlNumeric("42"); }
    SqlDate get_date(int) const override { return SqlDate(2023, 1, 1); }
    SqlTime get_time(int) const override { return SqlTime(12, 0, 0); }
    SqlTimestamp get_timestamp(int) const override { return SqlTimestamp(2023, 1, 1, 12, 0, 0); }
    SqlInterval get_interval(int) const override { return SqlInterval(); }
    SqlGuid get_guid(int) const override { return SqlGuid(); }
};

class DummyPreparedStatement : public IPreparedStatement {
public:
    void bind(int, const BoundValue&) override {}
    std::unique_ptr<IDataReader> execute_query() override { return nullptr; }
    size_t execute_non_query() override { return 0; }
    void reset() override {}
};

class TestOdbcConnection : public OdbcConnection {
public:
    using OdbcConnection::OdbcConnection;

    const ISqlDialect& dialect() const override {
        static PostgresDialect d;
        return d;
    }

    DriverCapabilities capabilities() const override {
        return {};
    }

    DriverInfo get_default_driver_info() const override {
        DriverInfo i;
        i.driver_name = "TestOdbcDriver";
        i.driver_version = "1.0.0";
        i.dbms_name = "TestDBMS";
        i.dbms_version = "1.0";
        i.odbc_version = "3.8";
        return i;
    }

    std::string get_driver_display_name() const override {
        return "TestOdbcDriver";
    }

    SQLHDBC get_hdbc() const { return hdbc_; }
    void set_hdbc(SQLHDBC h) { hdbc_ = h; }
};

extern "C" void _ZN7cpplinq11IDataReaderD0Ev(void*);
extern "C" void _ZN7cpplinq18IPreparedStatementD0Ev(void*);
extern "C" void _ZN7cpplinq11IConnectionD0Ev(void*);

TEST(ConnectionDefaultsTest, ExceptionsAndDefaults) {
    // UnsupportedFeature
    UnsupportedFeature uf_default;
    EXPECT_NE(std::string(uf_default.what()).find("not supported"), std::string::npos);

    UnsupportedFeature uf_custom("custom feature error");
    EXPECT_STREQ(uf_custom.what(), "custom feature error");

    // OperationCancelled
    OperationCancelled oc_default;
    EXPECT_NE(std::string(oc_default.what()).find("cancelled"), std::string::npos);

    OperationCancelled oc_custom("user requested abort");
    EXPECT_STREQ(oc_custom.what(), "user requested abort");

    // DummyPreparedStatement defaults
    DummyPreparedStatement stmt;
    EXPECT_THROW(stmt.cancel(), UnsupportedFeature);
    stmt.set_timeout(10);
    std::stop_source src;
    stmt.set_stop_token(src.get_token());

    // DummyDataReader defaults
    DummyDataReader reader;
    reader.return_null = true;
    auto val_null = reader.get_value(0);
    EXPECT_TRUE(std::holds_alternative<std::monostate>(val_null));

    reader.return_null = false;
    auto val_str = reader.get_value(0);
    EXPECT_TRUE(std::holds_alternative<std::string>(val_str));
    EXPECT_EQ(std::get<std::string>(val_str), "dummy_value");

    EXPECT_TRUE(reader.get_string_view(0).empty());

    // Base virtual destructors
    std::unique_ptr<IDataReader> r_ptr = std::make_unique<DummyDataReader>();
    r_ptr.reset();
    std::unique_ptr<IPreparedStatement> s_ptr = std::make_unique<DummyPreparedStatement>();
    s_ptr.reset();
    std::unique_ptr<IConnection> c_ptr = std::make_unique<TestOdbcConnection>("dummy");
    c_ptr.reset();

    // Calling deleting destructors directly to cover abstract base virtual destructors
    void* p1 = ::operator new(sizeof(IDataReader));
    _ZN7cpplinq11IDataReaderD0Ev(p1);

    void* p2 = ::operator new(sizeof(IPreparedStatement));
    _ZN7cpplinq18IPreparedStatementD0Ev(p2);

    void* p3 = ::operator new(sizeof(IConnection));
    _ZN7cpplinq11IConnectionD0Ev(p3);
}

// ----------------------------------------------------------------------------
// odbc_utils.h edge cases tests
// ----------------------------------------------------------------------------

TEST(OdbcUtilsTest, GuidEdgeCases) {
    // Valid GUIDs
    std::vector<std::string> valid_guids = {
        "00000000-0000-0000-0000-000000000000",
        "ffffffff-ffff-ffff-ffff-ffffffffffff",
        "12345678-9abc-def0-1234-56789abcdef0",
        "a1b2c3d4-e5f6-7a8b-9c0d-1e2f3a4b5c6d"
    };

    for (const auto& g_str : valid_guids) {
        SqlGuid g(g_str);
        EXPECT_EQ(g.to_string(), g_str);
        SQLGUID odbc_g = guid_to_odbc_struct(g);
        SqlGuid back_g = odbc_struct_to_guid(odbc_g);
        EXPECT_EQ(back_g, g);
        EXPECT_EQ(back_g.to_string(), g_str);
    }

    // Invalid GUIDs return nil guid
    EXPECT_TRUE(SqlGuid("invalid-guid-string").is_nil());
    EXPECT_TRUE(SqlGuid("short").is_nil());
    EXPECT_TRUE(SqlGuid("").is_nil());
    EXPECT_TRUE(SqlGuid("12345678-1234-1234-1234-12345678901z").is_nil());
}

TEST(OdbcUtilsTest, DateTimeTimestampStructs) {
    // Date
    SqlDate d(2023, 11, 29);
    DATE_STRUCT ds{static_cast<SQLSMALLINT>(d.year), d.month, d.day};
    EXPECT_EQ(ds.year, 2023);
    EXPECT_EQ(ds.month, 11);
    EXPECT_EQ(ds.day, 29);
    SqlDate d_back(ds.year, static_cast<uint8_t>(ds.month), static_cast<uint8_t>(ds.day));
    EXPECT_EQ(d_back, d);

    // Time
    SqlTime t(14, 45, 30, 654321);
    TIME_STRUCT ts_struct{t.hour, t.minute, t.second};
    EXPECT_EQ(ts_struct.hour, 14);
    EXPECT_EQ(ts_struct.minute, 45);
    EXPECT_EQ(ts_struct.second, 30);
    SqlTime t_back(static_cast<uint8_t>(ts_struct.hour), static_cast<uint8_t>(ts_struct.minute), static_cast<uint8_t>(ts_struct.second));
    EXPECT_EQ(t_back.hour, t.hour);
    EXPECT_EQ(t_back.minute, t.minute);
    EXPECT_EQ(t_back.second, t.second);

    // Timestamp
    SqlTimestamp ts(2024, 2, 29, 23, 59, 58, 987654);
    TIMESTAMP_STRUCT tss{static_cast<SQLSMALLINT>(ts.year), ts.month, ts.day, ts.hour, ts.minute, ts.second, static_cast<SQLUINTEGER>(ts.fraction)};
    EXPECT_EQ(tss.year, 2024);
    EXPECT_EQ(tss.month, 2);
    EXPECT_EQ(tss.day, 29);
    EXPECT_EQ(tss.hour, 23);
    EXPECT_EQ(tss.minute, 59);
    EXPECT_EQ(tss.second, 58);
    EXPECT_EQ(tss.fraction, 987654);
    SqlTimestamp ts_back(tss.year, static_cast<uint8_t>(tss.month), static_cast<uint8_t>(tss.day),
                         static_cast<uint8_t>(tss.hour), static_cast<uint8_t>(tss.minute), static_cast<uint8_t>(tss.second),
                         tss.fraction);
    EXPECT_EQ(ts_back, ts);
}

TEST(OdbcUtilsTest, IntervalAllTypesAndSigns) {
    std::vector<IntervalType> all_types = {
        IntervalType::Year,
        IntervalType::Month,
        IntervalType::YearToMonth,
        IntervalType::Day,
        IntervalType::Hour,
        IntervalType::Minute,
        IntervalType::Second,
        IntervalType::DayToHour,
        IntervalType::DayToMinute,
        IntervalType::DayToSecond,
        IntervalType::HourToMinute,
        IntervalType::HourToSecond,
        IntervalType::MinuteToSecond
    };

    for (auto type : all_types) {
        for (bool is_neg : {false, true}) {
            SqlInterval iv;
            iv.type = type;
            iv.years = 10;
            iv.months = 8;
            iv.days = 15;
            iv.hours = 18;
            iv.minutes = 45;
            iv.seconds = 30;
            iv.fraction = 500000;
            iv.is_negative = is_neg;

            SQL_INTERVAL_STRUCT odbc_s = interval_to_odbc_struct(iv);
            EXPECT_EQ(odbc_s.interval_sign, is_neg ? SQL_TRUE : SQL_FALSE);

            SqlInterval back = odbc_struct_to_interval(odbc_s);
            EXPECT_EQ(back.is_negative, is_neg);
            if (type == IntervalType::Year) {
                EXPECT_EQ(back.years, 10);
            } else if (type == IntervalType::Month) {
                EXPECT_EQ(back.months, 8);
            } else if (type == IntervalType::YearToMonth) {
                EXPECT_EQ(back.years, 10);
                EXPECT_EQ(back.months, 8);
            } else if (type == IntervalType::Day) {
                EXPECT_EQ(back.days, 15);
            } else if (type == IntervalType::Hour) {
                EXPECT_EQ(back.hours, 18);
            } else if (type == IntervalType::Minute) {
                EXPECT_EQ(back.minutes, 45);
            } else if (type == IntervalType::Second) {
                EXPECT_EQ(back.seconds, 30);
                EXPECT_EQ(back.fraction, 500000);
            }
        }
    }
}

TEST(OdbcUtilsTest, NumericConversionsEdgeCases) {
    // Leading '+' and scale padding
    SQL_NUMERIC_STRUCT ns_plus = string_to_numeric_struct("+123.4", 10, 3);
    EXPECT_EQ(ns_plus.sign, 1);
    EXPECT_EQ(numeric_struct_to_string(ns_plus), "123.400");

    // Scale truncation
    SQL_NUMERIC_STRUCT ns_trunc = string_to_numeric_struct("123.45678", 10, 2);
    EXPECT_EQ(numeric_struct_to_string(ns_trunc), "123.45");

    // Empty and whitespace strings
    SQL_NUMERIC_STRUCT ns_empty = string_to_numeric_struct("");
    EXPECT_EQ(numeric_struct_to_string(ns_empty), "0");

    SQL_NUMERIC_STRUCT ns_spaces = string_to_numeric_struct("   ");
    EXPECT_EQ(numeric_struct_to_string(ns_spaces), "0");

    // Negative scale
    SQL_NUMERIC_STRUCT ns_neg_scale{};
    ns_neg_scale.sign = 1;
    ns_neg_scale.scale = -2;
    ns_neg_scale.precision = 5;
    ns_neg_scale.val[0] = 5;
    EXPECT_EQ(numeric_struct_to_string(ns_neg_scale), "500");
}

TEST(OdbcUtilsTest, OdbcErrorHandles) {
    // Null handle
    EXPECT_EQ(get_odbc_error(SQL_HANDLE_STMT, SQL_NULL_HANDLE), "Unknown ODBC error");

    // Valid handle without errors
    SQLHENV henv = SQL_NULL_HENV;
    SQLRETURN rc = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv);
    ASSERT_TRUE(SQL_SUCCEEDED(rc));
    rc = SQLSetEnvAttr(henv, SQL_ATTR_ODBC_VERSION, reinterpret_cast<SQLPOINTER>(SQL_OV_ODBC3), 0);
    ASSERT_TRUE(SQL_SUCCEEDED(rc));

    std::string err_no_diag = get_odbc_error(SQL_HANDLE_ENV, henv);
    EXPECT_EQ(err_no_diag, "ODBC error (no diagnostic info)");

    // Allocate DBC and trigger connection error to get real diagnostic info
    SQLHDBC hdbc = SQL_NULL_HDBC;
    rc = SQLAllocHandle(SQL_HANDLE_DBC, henv, &hdbc);
    ASSERT_TRUE(SQL_SUCCEEDED(rc));

    SQLCHAR out_conn[256];
    SQLSMALLINT out_len = 0;
    std::string bad_conn_str = "Driver={NonExistentDriver_12345};Server=localhost;";
    rc = SQLDriverConnectA(hdbc, nullptr,
                           reinterpret_cast<SQLCHAR*>(bad_conn_str.data()),
                           static_cast<SQLSMALLINT>(bad_conn_str.size()),
                           out_conn, sizeof(out_conn), &out_len, SQL_DRIVER_NOPROMPT);
    EXPECT_FALSE(SQL_SUCCEEDED(rc));

    std::string err_diag = get_odbc_error(SQL_HANDLE_DBC, hdbc);
    EXPECT_NE(err_diag.find("["), std::string::npos);

    EXPECT_THROW({
        check_rc(rc, SQL_HANDLE_DBC, hdbc, "ConnectionAttempt");
    }, DbException);

    SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
    SQLFreeHandle(SQL_HANDLE_ENV, henv);
}

// ----------------------------------------------------------------------------
// OdbcConnection & OdbcDataReader & OdbcPreparedStatement tests
// ----------------------------------------------------------------------------

TEST(OdbcConnectionTest, ClosedOperationsThrow) {
    TestOdbcConnection conn("InvalidDSN");
    EXPECT_FALSE(conn.is_open());

    EXPECT_THROW(conn.prepare("SELECT 1;"), DbException);
    EXPECT_THROW(conn.execute("SELECT 1;"), DbException);
    EXPECT_THROW(conn.execute_query_direct("SELECT 1;"), DbException);
    EXPECT_THROW(conn.execute_non_query_direct("SELECT 1;"), DbException);
    EXPECT_THROW(conn.begin_transaction(), DbException);
    EXPECT_THROW(conn.commit(), DbException);
    EXPECT_THROW(conn.rollback(), DbException);

    auto di = conn.info();
    EXPECT_EQ(di.driver_name, "TestOdbcDriver");
    EXPECT_EQ(di.dbms_name, "TestDBMS");
}

TEST(OdbcConnectionTest, OpenFailure) {
    // Missing driver connect string
    TestOdbcConnection bad_conn("Driver={NonExistentDriver_999};Server=127.0.0.1;");
    EXPECT_THROW(bad_conn.open(), DbException);
    EXPECT_FALSE(bad_conn.is_open());

    // Single DSN word without '='
    TestOdbcConnection bad_dsn("NonExistentDSNName999");
    EXPECT_THROW(bad_dsn.open(), DbException);
    EXPECT_FALSE(bad_dsn.is_open());
}

TEST(OdbcConnectionTest, LiveOdbcExecutionAndTypes) {
    const char* env_dsn = std::getenv("CPPLINQ_POSTGRES_ODBC");
    std::string dsn = (env_dsn && env_dsn[0] != '\0') ? env_dsn : "PostgreSQL35W";

    TestOdbcConnection conn(dsn);
    try {
        conn.open();
    } catch (const std::exception& e) {
        GTEST_SKIP() << "ODBC DSN " << dsn << " could not be opened: " << e.what();
    }

    EXPECT_TRUE(conn.is_open());
    auto info = conn.info();
    EXPECT_FALSE(info.driver_name.empty());
    EXPECT_FALSE(info.dbms_name.empty());

    // Test execute
    conn.execute("CREATE TEMP TABLE test_odbc_coverage ("
                 "id INT, name VARCHAR(50), val FLOAT8, flag BOOLEAN, "
                 "bin_data BYTEA, num_val NUMERIC(10,2), d_val DATE, "
                 "t_val TIME, ts_val TIMESTAMP);");

    // Test insert_many_batch (exercises SQLBindParameter / SQLSetStmtAttr array binding)
    std::vector<BoundValue> batch_params = {
        int64_t{1}, std::string{"Alice"}, 10.5, true,
        std::vector<uint8_t>{1, 2, 3}, SqlNumeric("123.45"),
        SqlDate(2023, 1, 15), SqlTime(10, 30, 0), SqlTimestamp(2023, 1, 15, 10, 30, 0),

        int64_t{2}, std::string{"Bob"}, 20.25, false,
        std::vector<uint8_t>{4, 5}, SqlNumeric("678.90"),
        SqlDate(2023, 2, 20), SqlTime(14, 0, 0), SqlTimestamp(2023, 2, 20, 14, 0, 0),

        int64_t{3}, std::monostate{}, 0.0, true,
        std::vector<uint8_t>{}, SqlNumeric("0.00"),
        SqlDate(2023, 3, 25), SqlTime(18, 45, 0), SqlTimestamp(2023, 3, 25, 18, 45, 0)
    };

    size_t batch_inserted = conn.insert_many_batch(
        "INSERT INTO test_odbc_coverage (id, name, val, flag, bin_data, num_val, d_val, t_val, ts_val) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)",
        batch_params, 9, 3
    );
    EXPECT_EQ(batch_inserted, 3);

    // Test execute_non_query_direct
    size_t non_q = conn.execute_non_query_direct("UPDATE test_odbc_coverage SET val = 99.9 WHERE id = 1;");
    EXPECT_EQ(non_q, 1);

    // Test execute_query_direct and reader getters
    auto reader = conn.execute_query_direct(
        "SELECT id, name, val, flag, bin_data, num_val, d_val, t_val, ts_val "
        "FROM test_odbc_coverage ORDER BY id;"
    );
    EXPECT_EQ(reader->column_count(), 9);

    // Row 1
    EXPECT_TRUE(reader->next());
    EXPECT_EQ(reader->get_int64(0), 1);
    EXPECT_EQ(reader->get_uint64(0), 1);
    EXPECT_DOUBLE_EQ(reader->get_double(0), 1.0);
    EXPECT_EQ(reader->get_string(0), "1");
    EXPECT_EQ(reader->get_string_view(0), "1");
    EXPECT_EQ(reader->get_wstring(0), L"1");
    EXPECT_TRUE(reader->get_bool(0));

    EXPECT_EQ(reader->get_string(1), "Alice");
    EXPECT_EQ(reader->get_string_view(1), "Alice");
    EXPECT_EQ(reader->get_wstring(1), L"Alice");

    EXPECT_DOUBLE_EQ(reader->get_double(2), 99.9);
    EXPECT_EQ(reader->get_int64(2), 99);
    EXPECT_EQ(reader->get_uint64(2), 99);
    EXPECT_TRUE(reader->get_bool(2));

    EXPECT_TRUE(reader->get_bool(3));
    EXPECT_EQ(reader->get_int64(3), 1);
    EXPECT_EQ(reader->get_uint64(3), 1);
    EXPECT_DOUBLE_EQ(reader->get_double(3), 1.0);
    EXPECT_EQ(reader->get_string(3), "1");

    EXPECT_EQ(reader->get_blob(4), (std::vector<uint8_t>{1, 2, 3}));
    EXPECT_EQ(reader->get_numeric(5).to_string(), "123.45");
    EXPECT_EQ(reader->get_date(6).to_string(), "2023-01-15");
    EXPECT_EQ(reader->get_time(7).hour, 10);
    EXPECT_EQ(reader->get_timestamp(8).year, 2023);

    // get_value on each type (non-null)
    for (int col = 0; col < 9; ++col) {
        auto val = reader->get_value(col);
        EXPECT_FALSE(std::holds_alternative<std::monostate>(val));
    }
    EXPECT_EQ(std::get<int64_t>(reader->get_value(0)), 1);
    EXPECT_EQ(reader->get_string(1), "Alice");

    // Row 2
    EXPECT_TRUE(reader->next());
    EXPECT_FALSE(reader->get_bool(3));
    EXPECT_EQ(reader->get_string(3), "0");

    // Row 3 (null name)
    EXPECT_TRUE(reader->next());
    EXPECT_TRUE(reader->is_null(1));
    EXPECT_TRUE(std::holds_alternative<std::monostate>(reader->get_value(1)));
    EXPECT_TRUE(reader->get_string(1).empty());
    EXPECT_TRUE(reader->get_string_view(1).empty());
    EXPECT_TRUE(reader->get_wstring(1).empty());

    EXPECT_FALSE(reader->next());

    // Out of bounds check
    EXPECT_TRUE(reader->is_null(-1));
    EXPECT_TRUE(reader->is_null(100));

    // Test Prepared Statement with all types
    auto stmt = conn.prepare("SELECT ?::int, ?::text, ?::float8, ?::boolean, "
                             "?::bytea, ?::numeric, ?::date, ?::time, ?::timestamp");
    stmt->set_timeout(5);
    std::stop_source stop_src;
    stmt->set_stop_token(stop_src.get_token());
    stmt->cancel();

    EXPECT_THROW(stmt->bind(-1, 1), DbException);

    stmt->bind(0, int64_t{42});
    stmt->bind(1, std::string{"prepared_test"});
    stmt->bind(2, 3.14159);
    stmt->bind(3, true);
    stmt->bind(4, std::vector<uint8_t>{7, 8, 9});
    stmt->bind(5, SqlNumeric("50.25"));
    stmt->bind(6, SqlDate(2024, 7, 4));
    stmt->bind(7, SqlTime(12, 34, 56));
    stmt->bind(8, SqlTimestamp(2024, 7, 4, 12, 34, 56));

    auto p_reader = stmt->execute_query();
    EXPECT_TRUE(p_reader->next());
    EXPECT_EQ(p_reader->get_int64(0), 42);
    EXPECT_EQ(p_reader->get_string(1), "prepared_test");
    EXPECT_NEAR(p_reader->get_double(2), 3.14159, 1e-4);
    EXPECT_TRUE(p_reader->get_bool(3));

    stmt->reset();

    // Prepared statement non-query
    auto ins_stmt = conn.prepare("INSERT INTO test_odbc_coverage (id, name) VALUES (?, ?);");
    ins_stmt->bind(0, int64_t{10});
    ins_stmt->bind(1, std::string{"ten"});
    size_t ins_aff = ins_stmt->execute_non_query();
    EXPECT_EQ(ins_aff, 1);

    // Bind other variant types: uint64, wstring, null
    auto stmt2 = conn.prepare("SELECT ?::bigint, ?::text, ?::text");
    stmt2->bind(0, uint64_t{999999999999ULL});
    stmt2->bind(1, std::wstring{L"wide_param"});
    stmt2->bind(2, std::monostate{});
    auto r2 = stmt2->execute_query();
    EXPECT_TRUE(r2->next());
    EXPECT_EQ(r2->get_uint64(0), 999999999999ULL);
    EXPECT_EQ(r2->get_wstring(1), L"wide_param");
    EXPECT_TRUE(r2->is_null(2));

    // Test transactions
    conn.begin_transaction();
    conn.execute("UPDATE test_odbc_coverage SET val = 55.5 WHERE id = 1;");
    conn.commit();

    conn.begin_transaction();
    conn.execute("UPDATE test_odbc_coverage SET val = 0.0 WHERE id = 1;");
    conn.rollback();

    // Verify transaction rollback took effect (val should still be 55.5)
    auto check_r = conn.execute_query_direct("SELECT val FROM test_odbc_coverage WHERE id = 1;");
    EXPECT_TRUE(check_r->next());
    EXPECT_DOUBLE_EQ(check_r->get_double(0), 55.5);

    // Test OdbcDataReader with SQLHSTMT constructor
    OdbcDataReader null_stmt_reader(SQL_NULL_HSTMT, false);
    EXPECT_FALSE(null_stmt_reader.next());
    EXPECT_EQ(null_stmt_reader.column_count(), 0);
    EXPECT_TRUE(null_stmt_reader.is_null(0));

    // Conversions and ensure_str branches on OdbcDataReader
    auto r_conv = conn.execute_query_direct("SELECT '123' AS s_int, '123.45' AS s_dbl, 'true' AS s_bool, 456 AS i_num, 78.9 AS d_num;");
    EXPECT_TRUE(r_conv->next());
    EXPECT_EQ(r_conv->get_int64(0), 123);
    EXPECT_EQ(r_conv->get_uint64(0), 123);
    EXPECT_NEAR(r_conv->get_double(1), 123.45, 1e-2);
    EXPECT_TRUE(r_conv->get_bool(2));
    EXPECT_FALSE(r_conv->get_bool(4));
    EXPECT_NEAR(r_conv->get_double(4), 78.9, 1e-2);
    EXPECT_EQ(r_conv->get_string(3), "456");
    EXPECT_EQ(r_conv->get_string(4), "78.9");

    // Test stop_token request_stop callback
    auto stmt_stop = conn.prepare("SELECT 1;");
    std::stop_source stop_src_cancel;
    stmt_stop->set_stop_token(stop_src_cancel.get_token());
    stop_src_cancel.request_stop();

    // Test SqlGuid and SqlInterval binding & query
    auto stmt_guid = conn.prepare("SELECT ?::uuid, ?::interval;");
    stmt_guid->bind(0, SqlGuid("12345678-1234-5678-1234-567812345678"));
    SqlInterval iv_test;
    iv_test.type = IntervalType::Day;
    iv_test.days = 5;
    stmt_guid->bind(1, iv_test);
    auto r_guid = stmt_guid->execute_query();
    EXPECT_TRUE(r_guid->next());
    EXPECT_EQ(r_guid->get_guid(0).to_string(), "12345678-1234-5678-1234-567812345678");
    EXPECT_EQ(r_guid->get_interval(1).days, 5);
    EXPECT_EQ(r_guid->get_string(0), "12345678-1234-5678-1234-567812345678");

    // Direct query & non-query error paths
    EXPECT_THROW(conn.execute_query_direct("SELECT * FROM non_existent_table_999;"), DbException);
    EXPECT_THROW(conn.execute_non_query_direct("UPDATE non_existent_table_999 SET a = 1;"), DbException);

    // Test execute direct
    conn.execute("SELECT 1;");
    EXPECT_THROW(conn.execute("SELECT * FROM non_existent_table_err_direct;"), DbException);

    // Test info()
    auto cinfo = conn.info();
    EXPECT_FALSE(cinfo.driver_name.empty());
    EXPECT_FALSE(cinfo.dbms_name.empty());

    // Test OdbcPreparedStatement with empty driver_name and Oracle driver_name
    OdbcPreparedStatement oracle_stmt(conn.get_hdbc(), "SELECT 1;", "Oracle");
    oracle_stmt.bind(0, int64_t{42}); // triggers Oracle int64 binding
    oracle_stmt.execute_query();

    OdbcPreparedStatement empty_stmt(conn.get_hdbc(), "SELECT 1;", ""); // triggers DBMS_NAME fallback
    empty_stmt.execute_query();

    // PreparedStatement empty blob
    auto blob_stmt = conn.prepare("SELECT 1;");
    blob_stmt->bind(0, std::vector<uint8_t>{}); // triggers buffer.push_back(0)
    blob_stmt->execute_query();

    // PreparedStatement execution failure paths
    auto err_stmt = conn.prepare("SELECT * FROM non_existent_table_err_stmt;");
    EXPECT_THROW(err_stmt->execute_query(), DbException);
    EXPECT_THROW(err_stmt->execute_non_query(), DbException);

    // SQLEndTran commit/rollback error
    conn.begin_transaction();
    SQLHDBC real_hdbc = conn.get_hdbc();
    conn.set_hdbc(SQL_NULL_HDBC);
    EXPECT_THROW(conn.commit(), DbException);
    EXPECT_THROW(conn.rollback(), DbException);
    conn.set_hdbc(real_hdbc);
    conn.rollback();

    // Live reader via OdbcDataReader(SQLHSTMT, true)
    SQLHSTMT test_hstmt = SQL_NULL_HSTMT;
    SQLAllocHandle(SQL_HANDLE_STMT, conn.get_hdbc(), &test_hstmt);
    SQLExecDirectA(test_hstmt, reinterpret_cast<SQLCHAR*>(const_cast<char*>("SELECT 123 AS col1;")), SQL_NTS);
    OdbcDataReader live_reader(test_hstmt, true);
    EXPECT_TRUE(live_reader.next());
    EXPECT_EQ(live_reader.get_int64(0), 123);

    // Insert many batch exercising all types
    conn.execute("CREATE TEMP TABLE test_odbc_batch_full ("
                 "c_int BIGINT, c_uint NUMERIC(20), c_dbl FLOAT8, c_bit BOOLEAN, "
                 "c_str VARCHAR(50), c_wstr VARCHAR(50), c_bin BYTEA, "
                 "c_num NUMERIC, c_date DATE, c_time TIME, c_ts TIMESTAMP, "
                 "c_iv INTERVAL, c_guid UUID, c_null VARCHAR(10));");

    std::vector<BoundValue> full_batch_params;
    // Row 1: uint <= 0x7FFF..., wstring with surrogate, null, interval, guid
    full_batch_params.push_back(int64_t{10});
    full_batch_params.push_back(uint64_t{100}); // <= 0x7FFFFFFFFFFFFFFF
    full_batch_params.push_back(1.23);
    full_batch_params.push_back(true);
    full_batch_params.push_back(std::string{"row1"});
    full_batch_params.push_back(std::wstring{L"w\U0001F600"}); // surrogate
    full_batch_params.push_back(std::vector<uint8_t>{1, 2});
    full_batch_params.push_back(SqlNumeric("12.34"));
    full_batch_params.push_back(SqlDate(2023, 1, 1));
    full_batch_params.push_back(SqlTime(10, 0, 0));
    full_batch_params.push_back(SqlTimestamp(2023, 1, 1, 10, 0, 0));
    full_batch_params.push_back(SqlInterval());
    full_batch_params.push_back(SqlGuid("11111111-1111-1111-1111-111111111111"));
    full_batch_params.push_back(std::monostate{}); // null column triggers ct == SQL_C_DEFAULT

    // Row 2: uint > 0x7FFF...
    full_batch_params.push_back(int64_t{20});
    full_batch_params.push_back(uint64_t{0x8000000000000000ULL}); // > 0x7FFFFFFFFFFFFFFF
    full_batch_params.push_back(4.56);
    full_batch_params.push_back(false);
    full_batch_params.push_back(std::string{"row2"});
    full_batch_params.push_back(std::wstring{L"w2"});
    full_batch_params.push_back(std::vector<uint8_t>{3, 4});
    full_batch_params.push_back(SqlNumeric("56.78"));
    full_batch_params.push_back(SqlDate(2023, 1, 2));
    full_batch_params.push_back(SqlTime(11, 0, 0));
    full_batch_params.push_back(SqlTimestamp(2023, 1, 2, 11, 0, 0));
    full_batch_params.push_back(SqlInterval());
    full_batch_params.push_back(SqlGuid("22222222-2222-2222-2222-222222222222"));
    full_batch_params.push_back(std::monostate{});

    size_t inserted = conn.insert_many_batch(
        "INSERT INTO test_odbc_batch_full VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
        full_batch_params, 14, 2
    );
    EXPECT_EQ(inserted, 2);
}

TEST(OdbcDataReaderTest, AllGettersAndConversions) {
    OdbcDataReader reader(SQL_NULL_HSTMT, false);

    // 0: SQL_C_SBIGINT
    OdbcDataReader::BoundCol c0;
    c0.c_type = SQL_C_SBIGINT;
    c0.int_val = -12345;
    c0.ind = sizeof(int64_t);
    reader.bound_cols_.push_back(c0);

    // 1: SQL_C_UBIGINT
    OdbcDataReader::BoundCol c1;
    c1.c_type = SQL_C_UBIGINT;
    c1.uint_val = 987654321ULL;
    c1.ind = sizeof(uint64_t);
    reader.bound_cols_.push_back(c1);

    // 2: SQL_C_DOUBLE
    OdbcDataReader::BoundCol c2;
    c2.c_type = SQL_C_DOUBLE;
    c2.double_val = 3.14159;
    c2.ind = sizeof(double);
    reader.bound_cols_.push_back(c2);

    // 3: SQL_C_BIT (true)
    OdbcDataReader::BoundCol c3;
    c3.c_type = SQL_C_BIT;
    c3.bool_val = 1;
    c3.ind = 1;
    reader.bound_cols_.push_back(c3);

    // 4: SQL_C_CHAR ("12345")
    OdbcDataReader::BoundCol c4;
    c4.c_type = SQL_C_CHAR;
    std::string s4 = "12345";
    c4.buffer.assign(s4.begin(), s4.end());
    c4.buffer.push_back('\0');
    c4.ind = s4.size();
    reader.bound_cols_.push_back(c4);

    // 5: SQL_C_WCHAR (L"54321")
    OdbcDataReader::BoundCol c5;
    c5.c_type = SQL_C_WCHAR;
    std::wstring ws5 = L"54321";
    auto sqlw5 = wstring_to_sqlwchar(ws5);
    c5.buffer.resize(sqlw5.size() * sizeof(SQLWCHAR));
    std::memcpy(c5.buffer.data(), sqlw5.data(), c5.buffer.size());
    c5.ind = (sqlw5.size() - 1) * sizeof(SQLWCHAR);
    reader.bound_cols_.push_back(c5);

    // 6: SQL_C_BINARY
    OdbcDataReader::BoundCol c6;
    c6.c_type = SQL_C_BINARY;
    c6.buffer = {0x01, 0x02, 0x03, 0x04};
    c6.ind = 4;
    reader.bound_cols_.push_back(c6);

    // 7: SQL_C_TYPE_DATE
    OdbcDataReader::BoundCol c7;
    c7.c_type = SQL_C_TYPE_DATE;
    c7.date_val = DATE_STRUCT{2023, 5, 20};
    c7.ind = sizeof(DATE_STRUCT);
    reader.bound_cols_.push_back(c7);

    // 8: SQL_C_TYPE_TIME
    OdbcDataReader::BoundCol c8;
    c8.c_type = SQL_C_TYPE_TIME;
    c8.time_val = TIME_STRUCT{14, 30, 45};
    c8.ind = sizeof(TIME_STRUCT);
    reader.bound_cols_.push_back(c8);

    // 9: SQL_C_TYPE_TIMESTAMP
    OdbcDataReader::BoundCol c9;
    c9.c_type = SQL_C_TYPE_TIMESTAMP;
    c9.timestamp_val = TIMESTAMP_STRUCT{2023, 5, 20, 14, 30, 45, 123456000};
    c9.ind = sizeof(TIMESTAMP_STRUCT);
    reader.bound_cols_.push_back(c9);

    // 10: SQL_C_INTERVAL_DAY_TO_SECOND
    OdbcDataReader::BoundCol c10;
    c10.c_type = SQL_C_INTERVAL_DAY_TO_SECOND;
    SqlInterval iv10;
    iv10.type = IntervalType::DayToSecond;
    iv10.days = 2;
    iv10.hours = 4;
    iv10.minutes = 6;
    iv10.seconds = 8;
    c10.interval_val = interval_to_odbc_struct(iv10);
    c10.ind = sizeof(SQL_INTERVAL_STRUCT);
    reader.bound_cols_.push_back(c10);

    // 11: SQL_C_GUID
    OdbcDataReader::BoundCol c11;
    c11.c_type = SQL_C_GUID;
    c11.guid_val = guid_to_odbc_struct(SqlGuid("a1b2c3d4-e5f6-7a8b-9c0d-1e2f3a4b5c6d"));
    c11.ind = sizeof(SQLGUID);
    reader.bound_cols_.push_back(c11);

    // 12: SQL_C_CHAR with SQL_NUMERIC data_type
    OdbcDataReader::BoundCol c12;
    c12.c_type = SQL_C_CHAR;
    c12.data_type = SQL_NUMERIC;
    std::string s12 = "999.88";
    c12.buffer.assign(s12.begin(), s12.end());
    c12.buffer.push_back('\0');
    c12.ind = s12.size();
    reader.bound_cols_.push_back(c12);

    // 13: SQL_C_CHAR invalid number
    OdbcDataReader::BoundCol c13;
    c13.c_type = SQL_C_CHAR;
    std::string s13 = "not_a_number";
    c13.buffer.assign(s13.begin(), s13.end());
    c13.buffer.push_back('\0');
    c13.ind = s13.size();
    reader.bound_cols_.push_back(c13);

    // 14: SQL_C_WCHAR invalid number
    OdbcDataReader::BoundCol c14;
    c14.c_type = SQL_C_WCHAR;
    std::wstring ws14 = L"invalid_wide";
    auto sqlw14 = wstring_to_sqlwchar(ws14);
    c14.buffer.resize(sqlw14.size() * sizeof(SQLWCHAR));
    std::memcpy(c14.buffer.data(), sqlw14.data(), c14.buffer.size());
    c14.ind = (sqlw14.size() - 1) * sizeof(SQLWCHAR);
    reader.bound_cols_.push_back(c14);

    // 15: NULL column
    OdbcDataReader::BoundCol c15;
    c15.c_type = SQL_C_SBIGINT;
    c15.ind = SQL_NULL_DATA;
    reader.bound_cols_.push_back(c15);

    // 16: Negative ind for SQL_C_CHAR (triggers strnlen)
    OdbcDataReader::BoundCol c16;
    c16.c_type = SQL_C_CHAR;
    std::string s16 = "test_strnlen";
    c16.buffer.assign(s16.begin(), s16.end());
    c16.buffer.push_back('\0');
    c16.ind = SQL_NTS;
    reader.bound_cols_.push_back(c16);

    // 17: Negative ind for SQL_C_WCHAR
    OdbcDataReader::BoundCol c17;
    c17.c_type = SQL_C_WCHAR;
    std::wstring ws17 = L"test_wstr_neg";
    auto sqlw17 = wstring_to_sqlwchar(ws17);
    c17.buffer.resize(sqlw17.size() * sizeof(SQLWCHAR));
    std::memcpy(c17.buffer.data(), sqlw17.data(), c17.buffer.size());
    c17.ind = SQL_NTS;
    reader.bound_cols_.push_back(c17);

    // 18: Boolean string variations in CHAR
    OdbcDataReader::BoundCol c18;
    c18.c_type = SQL_C_CHAR;
    std::string s18 = "true";
    c18.buffer.assign(s18.begin(), s18.end());
    c18.buffer.push_back('\0');
    c18.ind = s18.size();
    reader.bound_cols_.push_back(c18);

    // 19: Boolean string variations in WCHAR
    OdbcDataReader::BoundCol c19;
    c19.c_type = SQL_C_WCHAR;
    std::wstring ws19 = L"true";
    auto sqlw19 = wstring_to_sqlwchar(ws19);
    c19.buffer.resize(sqlw19.size() * sizeof(SQLWCHAR));
    std::memcpy(c19.buffer.data(), sqlw19.data(), c19.buffer.size());
    c19.ind = (sqlw19.size() - 1) * sizeof(SQLWCHAR);
    reader.bound_cols_.push_back(c19);

    // Verify column_count and is_null
    reader.col_count_ = reader.bound_cols_.size();
    EXPECT_EQ(reader.column_count(), static_cast<int>(reader.bound_cols_.size()));
    EXPECT_FALSE(reader.is_null(0));
    EXPECT_TRUE(reader.is_null(15));
    EXPECT_TRUE(reader.is_null(-1));
    EXPECT_TRUE(reader.is_null(999));

    // get_int64 on all types
    EXPECT_EQ(reader.get_int64(0), -12345);
    EXPECT_EQ(reader.get_int64(1), 987654321);
    EXPECT_EQ(reader.get_int64(2), 3);
    EXPECT_EQ(reader.get_int64(3), 1);
    EXPECT_EQ(reader.get_int64(4), 12345);
    EXPECT_EQ(reader.get_int64(5), 54321);
    EXPECT_EQ(reader.get_int64(13), 0); // invalid parse catch
    EXPECT_EQ(reader.get_int64(14), 0); // invalid parse catch
    EXPECT_EQ(reader.get_int64(6), 0);  // default branch
    EXPECT_EQ(reader.get_int64(15), 0); // null

    // get_uint64 on all types
    EXPECT_EQ(reader.get_uint64(0), static_cast<uint64_t>(-12345));
    EXPECT_EQ(reader.get_uint64(1), 987654321ULL);
    EXPECT_EQ(reader.get_uint64(2), 3ULL);
    EXPECT_EQ(reader.get_uint64(3), 1ULL);
    EXPECT_EQ(reader.get_uint64(4), 12345ULL);
    EXPECT_EQ(reader.get_uint64(5), 54321ULL);
    EXPECT_EQ(reader.get_uint64(13), 0ULL);
    EXPECT_EQ(reader.get_uint64(14), 0ULL);
    EXPECT_EQ(reader.get_uint64(6), 0ULL);
    EXPECT_EQ(reader.get_uint64(15), 0ULL);

    // get_double on all types
    EXPECT_DOUBLE_EQ(reader.get_double(0), -12345.0);
    EXPECT_DOUBLE_EQ(reader.get_double(1), 987654321.0);
    EXPECT_NEAR(reader.get_double(2), 3.14159, 1e-4);
    EXPECT_DOUBLE_EQ(reader.get_double(3), 1.0);
    EXPECT_DOUBLE_EQ(reader.get_double(4), 12345.0);
    EXPECT_DOUBLE_EQ(reader.get_double(5), 54321.0);
    EXPECT_DOUBLE_EQ(reader.get_double(13), 0.0);
    EXPECT_DOUBLE_EQ(reader.get_double(14), 0.0);
    EXPECT_DOUBLE_EQ(reader.get_double(6), 0.0);
    EXPECT_DOUBLE_EQ(reader.get_double(15), 0.0);

    // get_bool on all types
    EXPECT_TRUE(reader.get_bool(0));
    EXPECT_TRUE(reader.get_bool(2));
    EXPECT_TRUE(reader.get_bool(3));
    EXPECT_TRUE(reader.get_bool(18));
    EXPECT_TRUE(reader.get_bool(19));
    EXPECT_FALSE(reader.get_bool(6)); // default
    EXPECT_FALSE(reader.get_bool(15)); // null

    // Boolean strings variations
    std::vector<std::string> true_strings = {"1", "true", "TRUE", "True", "t", "T", "yes", "YES", "Yes", "y", "Y"};
    for (const auto& ts : true_strings) {
        c18.buffer.assign(ts.begin(), ts.end());
        c18.buffer.push_back('\0');
        c18.ind = ts.size();
        reader.bound_cols_[18] = c18;
        reader.bound_cols_[18].str_cache.clear();
        EXPECT_TRUE(reader.get_bool(18));
    }
    std::vector<std::wstring> true_wstrings = {L"1", L"true", L"TRUE", L"True", L"t", L"T", L"yes", L"YES", L"Yes", L"y", L"Y"};
    for (const auto& tws : true_wstrings) {
        auto wbytes = wstring_to_sqlwchar(tws);
        c19.buffer.resize(wbytes.size() * sizeof(SQLWCHAR));
        std::memcpy(c19.buffer.data(), wbytes.data(), c19.buffer.size());
        c19.ind = (wbytes.size() - 1) * sizeof(SQLWCHAR);
        reader.bound_cols_[19] = c19;
        reader.bound_cols_[19].wstr_cache.clear();
        EXPECT_TRUE(reader.get_bool(19));
    }

    // get_string and get_string_view on all types
    EXPECT_EQ(reader.get_string(0), "-12345");
    EXPECT_EQ(reader.get_string(1), "987654321");
    EXPECT_FALSE(reader.get_string(2).empty());
    EXPECT_EQ(reader.get_string(3), "1");
    EXPECT_EQ(reader.get_string(4), "12345");
    EXPECT_EQ(reader.get_string(5), "54321");
    EXPECT_EQ(reader.get_string(7), "2023-05-20");
    EXPECT_EQ(reader.get_string(8), "14:30:45");
    EXPECT_FALSE(reader.get_string(9).empty());
    EXPECT_FALSE(reader.get_string(10).empty());
    EXPECT_EQ(reader.get_string(11), "a1b2c3d4-e5f6-7a8b-9c0d-1e2f3a4b5c6d");
    EXPECT_EQ(reader.get_string(16), "test_strnlen");
    EXPECT_EQ(reader.get_string(17), "test_wstr_neg");
    EXPECT_TRUE(reader.get_string(15).empty());

    // get_wstring
    EXPECT_EQ(reader.get_wstring(4), L"12345");
    EXPECT_EQ(reader.get_wstring(5), L"54321");
    EXPECT_TRUE(reader.get_wstring(15).empty());

    // get_blob
    auto b6 = reader.get_blob(6);
    EXPECT_EQ(b6.size(), 4);
    EXPECT_TRUE(reader.get_blob(0).empty());
    EXPECT_TRUE(reader.get_blob(15).empty());

    // get_numeric
    EXPECT_EQ(reader.get_numeric(0).to_string(), "-12345");
    EXPECT_FALSE(reader.get_numeric(2).to_string().empty());
    EXPECT_EQ(reader.get_numeric(4).to_string(), "12345");
    EXPECT_EQ(reader.get_numeric(12).to_string(), "999.88");
    EXPECT_EQ(reader.get_numeric(6).to_string(), "0");
    EXPECT_EQ(reader.get_numeric(15).to_string(), "0");

    // get_date
    EXPECT_EQ(reader.get_date(7).to_string(), "2023-05-20");
    EXPECT_EQ(reader.get_date(9).to_string(), "2023-05-20");
    EXPECT_EQ(reader.get_date(15).to_string(), "1970-01-01");

    // get_time
    EXPECT_EQ(reader.get_time(8).to_string(), "14:30:45");
    EXPECT_EQ(reader.get_time(9).to_string(), "14:30:45.123456000");
    EXPECT_EQ(reader.get_time(15).to_string(), "00:00:00");

    // get_timestamp
    EXPECT_EQ(reader.get_timestamp(7).to_string(), "2023-05-20 00:00:00");
    EXPECT_FALSE(reader.get_timestamp(9).to_string().empty());
    EXPECT_EQ(reader.get_timestamp(15).to_string(), "1970-01-01 00:00:00");

    // get_interval
    EXPECT_EQ(reader.get_interval(10).days, 2);
    EXPECT_EQ(reader.get_interval(15).days, 0);

    // get_guid
    EXPECT_EQ(reader.get_guid(11).to_string(), "a1b2c3d4-e5f6-7a8b-9c0d-1e2f3a4b5c6d");
    EXPECT_TRUE(reader.get_guid(15).is_nil());

    // get_value
    EXPECT_TRUE(std::holds_alternative<int64_t>(reader.get_value(0)));
    EXPECT_TRUE(std::holds_alternative<uint64_t>(reader.get_value(1)));
    EXPECT_TRUE(std::holds_alternative<double>(reader.get_value(2)));
    EXPECT_TRUE(std::holds_alternative<bool>(reader.get_value(3)));
    EXPECT_TRUE(std::holds_alternative<std::string>(reader.get_value(4)));
    EXPECT_TRUE(std::holds_alternative<std::wstring>(reader.get_value(5)));
    EXPECT_TRUE(std::holds_alternative<std::vector<uint8_t>>(reader.get_value(6)));
    EXPECT_TRUE(std::holds_alternative<SqlDate>(reader.get_value(7)));
    EXPECT_TRUE(std::holds_alternative<SqlTime>(reader.get_value(8)));
    EXPECT_TRUE(std::holds_alternative<SqlTimestamp>(reader.get_value(9)));
    EXPECT_TRUE(std::holds_alternative<SqlInterval>(reader.get_value(10)));
    EXPECT_TRUE(std::holds_alternative<SqlGuid>(reader.get_value(11)));
    EXPECT_TRUE(std::holds_alternative<SqlNumeric>(reader.get_value(12)));
    EXPECT_TRUE(std::holds_alternative<std::monostate>(reader.get_value(15)));
}

#endif
