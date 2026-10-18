#include "integration_test_suite.h"

#ifdef CPPLINQ_HAS_MYSQL
struct MysqlParams {
    using backend_type = cpplinq::mysql;
    static constexpr const char* name = "MySQL";

    static bool is_enabled() {
        const char* env_client = std::getenv("CPPLINQ_MYSQL_CLIENT");
        if (env_client != nullptr && env_client[0] != '\0') return true;
        const char* env_conn = std::getenv("CPPLINQ_MYSQL_ODBC");
        return env_conn != nullptr && env_conn[0] != '\0';
    }

    static std::string skip_reason() {
        return "CPPLINQ_MYSQL_CLIENT or CPPLINQ_MYSQL_ODBC environment variable is not set.";
    }

    static std::string connection_string() {
        const char* env_client = std::getenv("CPPLINQ_MYSQL_CLIENT");
        if (env_client && env_client[0] != '\0') return env_client;
        const char* env_conn = std::getenv("CPPLINQ_MYSQL_ODBC");
        return env_conn ? env_conn : "";
    }

    static void clean_tables(cpplinq::DbContext<backend_type>& db) {
        try {
            db.execute_raw("DROP TABLE IF EXISTS `test_measurements`");
            db.execute_raw("DROP TABLE IF EXISTS `test_employees`");
            db.execute_raw("DROP TABLE IF EXISTS `test_events`");
            db.execute_raw("DROP TABLE IF EXISTS `test_accounts`");
            db.execute_raw("DROP TABLE IF EXISTS `test_orders`");
            db.execute_raw("DROP TABLE IF EXISTS `test_users`");
        } catch (...) {}
    }
};

INSTANTIATE_TYPED_TEST_SUITE_P(Mysql, IntegrationTestSuite, MysqlParams);

// ----------------------------------------------------------------------------
// Direct MysqlConnection & MysqlDataReader & MysqlStmtDataReader coverage tests
// ----------------------------------------------------------------------------

TEST(MysqlDirectTest, DirectQueryAndReaderTypes) {
    if (!MysqlParams::is_enabled()) GTEST_SKIP() << MysqlParams::skip_reason();

    auto conn = make_connection<mysql>(MysqlParams::connection_string());
    conn->open();
    EXPECT_TRUE(conn->is_open());

    // info() and capabilities() and dialect()
    auto info = conn->info();
    EXPECT_EQ(info.driver_name, "MySQL Native Driver (libmariadb / libmysqlclient)");
    EXPECT_EQ(info.dbms_name, "MySQL");
    EXPECT_FALSE(info.driver_version.empty());
    EXPECT_FALSE(info.dbms_version.empty());
    EXPECT_EQ(info.odbc_version, "N/A");

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
    EXPECT_EQ(caps.default_batch_chunk_size, 5000);

    EXPECT_EQ(conn->dialect().quote_id("test"), "`test`");

    // Direct query
    auto reader = conn->execute_query_direct(
        "SELECT 1234567890123 AS b, 42 AS i, CAST(3.1415 AS DOUBLE) AS f, 'world' AS s, "
        "1 AS flag_t, 0 AS flag_f, X'01020304' AS bl, CAST(99.99 AS DECIMAL(10,2)) AS num, "
        "CAST('2023-04-05' AS DATE) AS d, CAST('11:22:33' AS TIME) AS t, "
        "CAST('2023-04-05 11:22:33' AS DATETIME) AS ts, NULL AS null_val, "
        "'2 days' AS iv, '12345678-1234-5678-1234-567812345678' AS g;"
    );
    EXPECT_EQ(reader->column_count(), 14);
    EXPECT_TRUE(reader->next());

    EXPECT_EQ(reader->get_int64(0), 1234567890123LL);
    EXPECT_EQ(reader->get_uint64(0), 1234567890123ULL);
    EXPECT_EQ(reader->get_int64(1), 42);
    EXPECT_NEAR(reader->get_double(2), 3.1415, 1e-4);
    EXPECT_EQ(reader->get_string(3), "world");
    EXPECT_EQ(reader->get_string_view(3), "world");
    EXPECT_EQ(reader->get_wstring(3), L"world");
    EXPECT_TRUE(reader->get_bool(4));
    EXPECT_FALSE(reader->get_bool(5));
    EXPECT_EQ(reader->get_blob(6), (std::vector<uint8_t>{1, 2, 3, 4}));
    EXPECT_EQ(reader->get_numeric(7).to_string(), "99.99");
    EXPECT_EQ(reader->get_date(8).to_string(), "2023-04-05");
    EXPECT_EQ(reader->get_time(9).hour, 11);
    EXPECT_EQ(reader->get_timestamp(10).year, 2023);
    EXPECT_EQ(reader->get_interval(12).days, 2);
    EXPECT_EQ(reader->get_guid(13).to_string(), "12345678-1234-5678-1234-567812345678");

    // Out of bounds and null checks
    EXPECT_FALSE(reader->is_null(0));
    EXPECT_TRUE(reader->is_null(11));
    EXPECT_TRUE(reader->is_null(-1));
    EXPECT_TRUE(reader->is_null(999));
    EXPECT_EQ(reader->get_int64(11), 0);
    EXPECT_EQ(reader->get_uint64(11), 0);
    EXPECT_EQ(reader->get_double(11), 0.0);
    EXPECT_TRUE(reader->get_string(11).empty());
    EXPECT_TRUE(reader->get_string_view(11).empty());
    EXPECT_TRUE(reader->get_wstring(11).empty());
    EXPECT_FALSE(reader->get_bool(11));
    EXPECT_TRUE(reader->get_blob(11).empty());

    // Test get_value
    auto v_int = reader->get_value(0);
    EXPECT_EQ(std::get<int64_t>(v_int), 1234567890123LL);

    auto v_dbl = reader->get_value(2);
    EXPECT_NEAR(std::get<double>(v_dbl), 3.1415, 1e-4);

    auto v_str = reader->get_value(3);
    EXPECT_EQ(std::get<std::string>(v_str), "world");

    auto v_blob = reader->get_value(6);
    EXPECT_FALSE(std::holds_alternative<std::monostate>(v_blob));

    auto v_null = reader->get_value(11);
    EXPECT_TRUE(std::holds_alternative<std::monostate>(v_null));

    // Bool characters variations: '1', 't', 'T', 'y', 'Y'
    auto r_bools = conn->execute_query_direct("SELECT 't', 'T', '1', 'y', 'Y', 'f', '0';");
    EXPECT_TRUE(r_bools->next());
    for (int col = 0; col < 5; ++col) {
        EXPECT_TRUE(r_bools->get_bool(col));
    }
    EXPECT_FALSE(r_bools->get_bool(5));
    EXPECT_FALSE(r_bools->get_bool(6));

    // Test transaction methods
    conn->begin_transaction();
    conn->commit();
    conn->begin_transaction();
    conn->rollback();

    // Test multi-statement execute
    conn->execute("SELECT 1; SELECT 2;");

    EXPECT_FALSE(reader->next());
}

TEST(MysqlDirectTest, DirectNonQueryAndTempTable) {
    if (!MysqlParams::is_enabled()) GTEST_SKIP() << MysqlParams::skip_reason();

    auto conn = make_connection<mysql>(MysqlParams::connection_string());
    conn->open();

    conn->execute_non_query_direct("CREATE TEMPORARY TABLE my_tmp_direct(id INT, v VARCHAR(50));");
    size_t affected = conn->execute_non_query_direct("INSERT INTO my_tmp_direct VALUES (10, 'x'), (20, 'y');");
    EXPECT_EQ(affected, 2);

    size_t upd = conn->execute_non_query_direct("UPDATE my_tmp_direct SET v = 'updated' WHERE id = 10;");
    EXPECT_EQ(upd, 1);

    size_t del = conn->execute_non_query_direct("DELETE FROM my_tmp_direct WHERE id = 20;");
    EXPECT_EQ(del, 1);
}

TEST(MysqlDirectTest, InsertManyBatchValuesFastPath) {
    if (!MysqlParams::is_enabled()) GTEST_SKIP() << MysqlParams::skip_reason();

    auto conn = make_connection<mysql>(MysqlParams::connection_string());
    conn->open();

    conn->execute("CREATE TEMPORARY TABLE my_tmp_batch ("
                 "id INT, v VARCHAR(150), f DOUBLE, b TINYINT, bl BLOB, "
                 "num DECIMAL(10,2), d DATE, t TIME, ts DATETIME, u BIGINT UNSIGNED, "
                 "g VARCHAR(36), iv VARCHAR(50));");

    // Zero rows check
    EXPECT_EQ(conn->insert_many_batch("INSERT INTO my_tmp_batch (id, v) VALUES (?, ?)", {}, 2, 0), 0);

    SqlInterval iv_val;
    iv_val.type = IntervalType::Day;
    iv_val.days = 7;

    std::vector<BoundValue> params = {
        int64_t{1}, std::string{"it's a \"quoted\" \\escaped string"}, 12.34, true,
        std::vector<uint8_t>{0xaa, 0xbb}, SqlNumeric("100.50"), SqlDate(2023, 5, 1),
        SqlTime(8, 0, 0), SqlTimestamp(2023, 5, 1, 8, 0, 0), uint64_t{12345},
        SqlGuid("12345678-1234-5678-1234-567812345678"), iv_val,

        int64_t{2}, std::monostate{}, 56.78, false,
        std::vector<uint8_t>{}, SqlNumeric("200.75"), SqlDate(2023, 5, 2),
        SqlTime(9, 15, 0), SqlTimestamp(2023, 5, 2, 9, 15, 0), uint64_t{67890},
        SqlGuid("87654321-4321-8765-4321-876543210987"), iv_val,

        int64_t{3}, std::wstring{L"wide_str"}, 90.12, true,
        std::vector<uint8_t>{1, 2, 3}, SqlNumeric("300.00"), SqlDate(2023, 5, 3),
        SqlTime(10, 30, 0), SqlTimestamp(2023, 5, 3, 10, 30, 0), uint64_t{99999},
        SqlGuid("00000000-0000-0000-0000-000000000000"), iv_val
    };

    size_t count = conn->insert_many_batch(
        "INSERT INTO my_tmp_batch (id, v, f, b, bl, num, d, t, ts, u, g, iv) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
        params, 12, 3
    );
    EXPECT_EQ(count, 3);

    auto r = conn->execute_query_direct("SELECT v FROM my_tmp_batch WHERE id = 1;");
    EXPECT_TRUE(r->next());
    EXPECT_EQ(r->get_string(0), "it's a \"quoted\" \\escaped string");

    // Large batch crossing BATCH_CHUNK = 2000
    constexpr size_t TOTAL_ROWS = 2100;
    std::vector<BoundValue> large_params;
    large_params.reserve(TOTAL_ROWS * 2);
    for (size_t i = 0; i < TOTAL_ROWS; ++i) {
        large_params.push_back(int64_t{10000 + static_cast<int64_t>(i)});
        large_params.push_back(std::string{"row_"} + std::to_string(i));
    }
    size_t large_count = conn->insert_many_batch(
        "INSERT INTO my_tmp_batch (id, v) VALUES (?, ?)",
        large_params, 2, TOTAL_ROWS
    );
    EXPECT_EQ(large_count, TOTAL_ROWS);
}

TEST(MysqlDirectTest, InsertManyBatchFallbackLoop) {
    if (!MysqlParams::is_enabled()) GTEST_SKIP() << MysqlParams::skip_reason();

    auto conn = make_connection<mysql>(MysqlParams::connection_string());
    conn->open();

    conn->execute("CREATE TEMPORARY TABLE my_tmp_fallback (id INT, v VARCHAR(50));");

    std::vector<BoundValue> params = {
        int64_t{100}, std::string{"fall_mysql_1"},
        int64_t{200}, std::string{"fall_mysql_2"}
    };
    // Non-standard INSERT (SELECT instead of VALUES) uses prepared statement fallback loop
    size_t affected = conn->insert_many_batch(
        "INSERT INTO my_tmp_fallback (id, v) SELECT ?, ?",
        params, 2, 2
    );
    EXPECT_EQ(affected, 2);
}

TEST(MysqlDirectTest, PreparedStatementFeatures) {
    if (!MysqlParams::is_enabled()) GTEST_SKIP() << MysqlParams::skip_reason();

    auto conn = make_connection<mysql>(MysqlParams::connection_string());
    conn->open();

    auto stmt = conn->prepare(
        "SELECT ? AS id, ? AS v, ? AS f, ? AS b, ? AS bl, "
        "? AS num, ? AS d, ? AS t, ? AS ts, ? AS u, ? AS w, ? AS nl;"
    );

    EXPECT_THROW(stmt->bind(-1, 1), DbException);

    stmt->bind(0, int64_t{100});
    stmt->bind(1, std::string{"prep_val"});
    stmt->bind(2, 45.67);
    stmt->bind(3, true);
    stmt->bind(4, std::vector<uint8_t>{0xde, 0xad});
    stmt->bind(5, SqlNumeric("12.34"));
    stmt->bind(6, SqlDate(2024, 6, 1));
    stmt->bind(7, SqlTime(15, 30, 0));
    stmt->bind(8, SqlTimestamp(2024, 6, 1, 15, 30, 0));
    stmt->bind(9, uint64_t{7777777777ULL});
    stmt->bind(10, std::wstring{L"wide_prep"});
    stmt->bind(11, std::monostate{});

    auto reader = stmt->execute_query();
    EXPECT_EQ(reader->column_count(), 12);
    EXPECT_TRUE(reader->next());

    EXPECT_EQ(reader->get_int64(0), 100);
    EXPECT_EQ(reader->get_string(1), "prep_val");
    EXPECT_EQ(reader->get_string_view(1), "prep_val");
    EXPECT_EQ(reader->get_wstring(1), L"prep_val");
    EXPECT_NEAR(reader->get_double(2), 45.67, 1e-4);
    EXPECT_TRUE(reader->get_bool(3));
    EXPECT_EQ(reader->get_blob(4), (std::vector<uint8_t>{0xde, 0xad}));
    EXPECT_EQ(reader->get_numeric(5).to_string(), "12.34");
    EXPECT_EQ(reader->get_date(6).to_string(), "2024-06-01");
    EXPECT_EQ(reader->get_time(7).hour, 15);
    EXPECT_EQ(reader->get_timestamp(8).year, 2024);
    EXPECT_EQ(reader->get_uint64(9), 7777777777ULL);
    EXPECT_EQ(reader->get_wstring(10), L"wide_prep");
    EXPECT_TRUE(reader->is_null(11));
    EXPECT_TRUE(reader->is_null(-1));
    EXPECT_TRUE(reader->is_null(99));

    // MysqlStmtDataReader get_value
    auto v_int = reader->get_value(0);
    EXPECT_EQ(std::get<int64_t>(v_int), 100);

    auto v_dbl = reader->get_value(2);
    EXPECT_NEAR(std::get<double>(v_dbl), 45.67, 1e-4);

    auto v_str = reader->get_value(1);
    EXPECT_EQ(std::get<std::string>(v_str), "prep_val");

    auto v_null = reader->get_value(11);
    EXPECT_TRUE(std::holds_alternative<std::monostate>(v_null));

    stmt->set_timeout(10);
    std::stop_source stop_src;
    stmt->set_stop_token(stop_src.get_token());
    stmt->cancel();
    stmt->reset();

    // Prepared statement non-query
    conn->execute("CREATE TEMPORARY TABLE my_tmp_prep(id INT, v VARCHAR(50));");
    auto ins_stmt = conn->prepare("INSERT INTO my_tmp_prep VALUES (?, ?);");
    ins_stmt->bind(0, int64_t{1});
    ins_stmt->bind(1, std::string{"one"});
    size_t aff = ins_stmt->execute_non_query();
    EXPECT_EQ(aff, 1);

    // Prepared statement error cases
    EXPECT_THROW({
        conn->prepare("SYNTAX ERROR NOT SQL");
    }, DbException);

    // Non-query on bad table
    EXPECT_THROW({
        auto bad_exec = conn->prepare("INSERT INTO non_existent_tbl_xyz VALUES (?);");
        bad_exec->bind(0, int64_t{1});
        bad_exec->execute_non_query();
    }, DbException);

    // Prepared statement with SqlGuid and SqlInterval
    auto g_stmt = conn->prepare("SELECT ? AS g, ? AS iv;");
    g_stmt->bind(0, SqlGuid("12345678-1234-5678-1234-567812345678"));
    SqlInterval iv_m;
    iv_m.type = IntervalType::Day;
    iv_m.days = 10;
    g_stmt->bind(1, iv_m);
    auto g_r = g_stmt->execute_query();
    EXPECT_TRUE(g_r->next());
    EXPECT_EQ(g_r->get_guid(0).to_string(), "12345678-1234-5678-1234-567812345678");
    EXPECT_EQ(g_r->get_interval(1).days, 10);

    // execute_query on statement producing no result set (metadata failure)
    auto no_res_stmt = conn->prepare("INSERT INTO my_tmp_prep VALUES (?, ?);");
    no_res_stmt->bind(0, int64_t{99});
    no_res_stmt->bind(1, std::string{"no_res"});
    EXPECT_THROW(no_res_stmt->execute_query(), DbException);

    // Direct query & non-query & execute error paths
    EXPECT_THROW(conn->execute_query_direct("SELECT * FROM non_existent_table_99999;"), DbException);
    EXPECT_THROW(conn->execute_non_query_direct("UPDATE non_existent_table_99999 SET x = 1;"), DbException);
    EXPECT_THROW(conn->execute("INVALID SQL SYNTAX;"), DbException);
}

TEST(MysqlDirectTest, ConnectionParamsAndClosedErrors) {
    // Closed connection throwing DbException
    {
        auto conn = make_connection<mysql>("host=127.0.0.1");
        EXPECT_FALSE(conn->is_open());
        EXPECT_THROW(conn->prepare("SELECT 1;"), DbException);
        EXPECT_THROW(conn->execute("SELECT 1;"), DbException);
        EXPECT_THROW(conn->execute_query_direct("SELECT 1;"), DbException);
        EXPECT_THROW(conn->execute_non_query_direct("SELECT 1;"), DbException);
        EXPECT_THROW(conn->begin_transaction(), DbException);
        EXPECT_THROW(conn->commit(), DbException);
        EXPECT_THROW(conn->rollback(), DbException);
        EXPECT_THROW(conn->insert_many_batch("INSERT INTO t VALUES (?)", {int64_t{1}}, 1, 1), DbException);
    }

    // Invalid connection throwing DbException
    {
        auto conn = make_connection<mysql>("host=127.0.0.1;port=19999;user=invalid;password=invalid;timeout=1;");
        EXPECT_THROW(conn->open(), DbException);
    }

    // URI format without password
    {
        std::string uri2 = "mysql://cppdb@127.0.0.1:3306/cppdb";
        auto conn = make_connection<mysql>(uri2);
    }

    // Key-value format with socket parameter
    {
        std::string kv_sock = "host=127.0.0.1;port=3306;db=cppdb;user=cppdb;password=cppdb_password;socket=/tmp/mysql.sock;";
        auto conn = make_connection<mysql>(kv_sock);
    }

    if (!MysqlParams::is_enabled()) {
        GTEST_SKIP() << MysqlParams::skip_reason();
    }

    // Empty connection string (falls back to env)
    {
        auto conn = make_connection<mysql>("");
        EXPECT_NO_THROW(conn->open());
        EXPECT_TRUE(conn->is_open());
        conn->close();
        EXPECT_FALSE(conn->is_open());
    }

    // URI format with port and charset
    {
        std::string uri = "mysql://cppdb:cppdb_password@127.0.0.1:3306/cppdb?charset=utf8mb4";
        auto conn = make_connection<mysql>(uri);
        EXPECT_NO_THROW(conn->open());
        EXPECT_TRUE(conn->is_open());
    }

    // URI format without port
    {
        std::string uri_no_port = "mysql://cppdb:cppdb_password@127.0.0.1/cppdb";
        auto conn = make_connection<mysql>(uri_no_port);
        EXPECT_NO_THROW(conn->open());
        EXPECT_TRUE(conn->is_open());
    }

    // Key-value format with curly braces and variations
    {
        std::string kv = "host=127.0.0.1;port=3306;db=cppdb;user=cppdb;password={cppdb_password};charset=utf8mb4;";
        auto conn = make_connection<mysql>(kv);
        EXPECT_NO_THROW(conn->open());
        EXPECT_TRUE(conn->is_open());
    }

    // Single DSN word
    {
        auto conn = make_connection<mysql>("MySQLDSN");
        EXPECT_NO_THROW(conn->open());
        EXPECT_TRUE(conn->is_open());
    }
}

#endif

