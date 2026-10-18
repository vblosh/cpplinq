#include "integration_test_suite.h"

#ifdef CPPLINQ_HAS_POSTGRES
struct PostgresParams {
    using backend_type = cpplinq::postgres;
    static constexpr const char* name = "PostgreSQL";

    static bool is_enabled() {
        const char* env_libpq = std::getenv("CPPLINQ_POSTGRES_LIBPQ");
        if (env_libpq != nullptr && env_libpq[0] != '\0') return true;
        const char* env_odbc = std::getenv("CPPLINQ_POSTGRES_ODBC");
        return env_odbc != nullptr && env_odbc[0] != '\0';
    }

    static std::string skip_reason() {
        return "CPPLINQ_POSTGRES_LIBPQ or CPPLINQ_POSTGRES_ODBC environment variable is not set.";
    }

    static std::string connection_string() {
        const char* env_libpq = std::getenv("CPPLINQ_POSTGRES_LIBPQ");
        if (env_libpq && env_libpq[0] != '\0') return env_libpq;
        const char* env_odbc = std::getenv("CPPLINQ_POSTGRES_ODBC");
        return env_odbc ? env_odbc : "";
    }

    static void clean_tables(cpplinq::DbContext<backend_type>& db) {
        try {
            db.execute_raw("DROP TABLE IF EXISTS \"test_measurements\" CASCADE");
            db.execute_raw("DROP TABLE IF EXISTS \"test_employees\" CASCADE");
            db.execute_raw("DROP TABLE IF EXISTS \"test_events\" CASCADE");
            db.execute_raw("DROP TABLE IF EXISTS \"test_accounts\" CASCADE");
            db.execute_raw("DROP TABLE IF EXISTS \"test_orders\" CASCADE");
            db.execute_raw("DROP TABLE IF EXISTS \"test_users\" CASCADE");
        } catch (...) {}
    }
};

INSTANTIATE_TYPED_TEST_SUITE_P(Postgres, IntegrationTestSuite, PostgresParams);

// ----------------------------------------------------------------------------
// Direct libpq PgConnection & PgDataReader & PgPreparedStatement coverage tests
// ----------------------------------------------------------------------------

TEST(PostgresIntegrationTest, DirectQueryAndReaderTypes) {
    if (!PostgresParams::is_enabled()) GTEST_SKIP() << PostgresParams::skip_reason();

    auto conn = make_connection<postgres>(PostgresParams::connection_string());
    conn->open();
    EXPECT_TRUE(conn->is_open());

    // conn->info() and conn->capabilities()
    auto info = conn->info();
    EXPECT_EQ(info.driver_name, "PostgreSQL Native Driver (libpq)");
    EXPECT_EQ(info.dbms_name, "PostgreSQL");
    EXPECT_FALSE(info.driver_version.empty());
    EXPECT_FALSE(info.dbms_version.empty());
    EXPECT_EQ(info.odbc_version, "N/A");

    auto caps = conn->capabilities();
    EXPECT_TRUE(caps.cancel);
    EXPECT_TRUE(caps.streaming);
    EXPECT_TRUE(caps.query_timeout);
    EXPECT_TRUE(caps.transactions);
    EXPECT_TRUE(caps.savepoints);
    EXPECT_TRUE(caps.returning_clause);
    EXPECT_FALSE(caps.output_clause);
    EXPECT_TRUE(caps.upsert);
    EXPECT_TRUE(caps.array_batch_insert);
    EXPECT_EQ(caps.default_batch_chunk_size, 50000);
    EXPECT_TRUE(caps.window_functions);
    EXPECT_TRUE(caps.ctes);

    // Direct query
    auto reader = conn->execute_query_direct(
        "SELECT 1234567890123::bigint AS b, 42::int4 AS i, 3.1415::float8 AS f, 'hello'::text AS s, "
        "TRUE AS flag_t, FALSE AS flag_f, '\\x01020304'::bytea AS bl, 99.99::numeric AS num, "
        "'2023-04-05'::date AS d, '11:22:33'::time AS t, '2023-04-05 11:22:33'::timestamp AS ts, "
        "'2 days 3 hours'::interval AS iv, NULL::text AS null_val;"
    );
    EXPECT_EQ(reader->column_count(), 13);
    EXPECT_TRUE(reader->next());

    EXPECT_EQ(reader->get_int64(0), 1234567890123LL);
    EXPECT_EQ(reader->get_uint64(0), 1234567890123ULL);
    EXPECT_EQ(reader->get_int64(1), 42);
    EXPECT_NEAR(reader->get_double(2), 3.1415, 1e-4);
    EXPECT_EQ(reader->get_string(3), "hello");
    EXPECT_EQ(reader->get_string_view(3), "hello");
    EXPECT_EQ(reader->get_wstring(3), L"hello");
    EXPECT_TRUE(reader->get_bool(4));
    EXPECT_FALSE(reader->get_bool(5));
    EXPECT_EQ(reader->get_blob(6), (std::vector<uint8_t>{1, 2, 3, 4}));
    EXPECT_EQ(reader->get_numeric(7).to_string(), "99.99");
    EXPECT_EQ(reader->get_date(8).to_string(), "2023-04-05");
    EXPECT_EQ(reader->get_time(9).hour, 11);
    EXPECT_EQ(reader->get_timestamp(10).year, 2023);
    EXPECT_EQ(reader->get_interval(11).days, 2);

    // Out of bounds and null checks
    EXPECT_FALSE(reader->is_null(0));
    EXPECT_TRUE(reader->is_null(12));
    EXPECT_TRUE(reader->is_null(-1));
    EXPECT_TRUE(reader->is_null(999));
    EXPECT_EQ(reader->get_int64(12), 0);
    EXPECT_EQ(reader->get_uint64(12), 0);
    EXPECT_EQ(reader->get_double(12), 0.0);
    EXPECT_TRUE(reader->get_string(12).empty());
    EXPECT_TRUE(reader->get_string_view(12).empty());
    EXPECT_TRUE(reader->get_wstring(12).empty());
    EXPECT_FALSE(reader->get_bool(12));
    EXPECT_TRUE(reader->get_blob(12).empty());

    // Test get_value for all OIDs
    auto v_int8 = reader->get_value(0);
    EXPECT_TRUE(std::holds_alternative<int64_t>(v_int8));
    EXPECT_EQ(std::get<int64_t>(v_int8), 1234567890123LL);

    auto v_int4 = reader->get_value(1);
    EXPECT_TRUE(std::holds_alternative<int64_t>(v_int4));
    EXPECT_EQ(std::get<int64_t>(v_int4), 42);

    auto v_dbl = reader->get_value(2);
    EXPECT_TRUE(std::holds_alternative<double>(v_dbl));
    EXPECT_NEAR(std::get<double>(v_dbl), 3.1415, 1e-4);

    auto v_str = reader->get_value(3);
    EXPECT_TRUE(std::holds_alternative<std::string>(v_str));
    EXPECT_EQ(std::get<std::string>(v_str), "hello");

    auto v_bool = reader->get_value(4);
    EXPECT_TRUE(std::holds_alternative<bool>(v_bool));
    EXPECT_TRUE(std::get<bool>(v_bool));

    auto v_blob = reader->get_value(6);
    EXPECT_TRUE(std::holds_alternative<std::vector<uint8_t>>(v_blob));
    EXPECT_EQ(std::get<std::vector<uint8_t>>(v_blob), (std::vector<uint8_t>{1, 2, 3, 4}));

    auto v_num = reader->get_value(7);
    EXPECT_TRUE(std::holds_alternative<double>(v_num));
    EXPECT_NEAR(std::get<double>(v_num), 99.99, 1e-2);

    auto v_null = reader->get_value(12);
    EXPECT_TRUE(std::holds_alternative<std::monostate>(v_null));

    // Bool characters variations: 't', 'T', '1', 'y', 'Y'
    auto r_bools = conn->execute_query_direct("SELECT 't'::text, 'T'::text, '1'::text, 'y'::text, 'Y'::text, 'f'::text, '0'::text;");
    EXPECT_TRUE(r_bools->next());
    for (int col = 0; col < 5; ++col) {
        EXPECT_TRUE(r_bools->get_bool(col));
    }
    EXPECT_FALSE(r_bools->get_bool(5));
    EXPECT_FALSE(r_bools->get_bool(6));

    // Test transaction methods & dialect
    conn->begin_transaction();
    conn->commit();
    conn->begin_transaction();
    conn->rollback();
    EXPECT_EQ(conn->dialect().quote_id("t"), "\"t\"");

    EXPECT_FALSE(reader->next());
}

TEST(PostgresIntegrationTest, DirectNonQueryAndTempTable) {
    if (!PostgresParams::is_enabled()) GTEST_SKIP() << PostgresParams::skip_reason();

    auto conn = make_connection<postgres>(PostgresParams::connection_string());
    conn->open();

    conn->execute_non_query_direct("CREATE TEMP TABLE pg_tmp_direct(id INT, v TEXT);");
    size_t affected = conn->execute_non_query_direct("INSERT INTO pg_tmp_direct VALUES (1, 'a'), (2, 'b');");
    EXPECT_EQ(affected, 2);

    size_t upd = conn->execute_non_query_direct("UPDATE pg_tmp_direct SET v = 'updated' WHERE id = 1;");
    EXPECT_EQ(upd, 1);

    size_t del = conn->execute_non_query_direct("DELETE FROM pg_tmp_direct WHERE id = 2;");
    EXPECT_EQ(del, 1);
}

TEST(PostgresIntegrationTest, InsertManyBatchCopyFastPath) {
    if (!PostgresParams::is_enabled()) GTEST_SKIP() << PostgresParams::skip_reason();

    auto conn = make_connection<postgres>(PostgresParams::connection_string());
    conn->open();

    conn->execute("CREATE TEMP TABLE pg_tmp_batch ("
                 "id INT, v TEXT, f FLOAT8, b BOOLEAN, bl BYTEA, "
                 "d DATE, t TIME, ts TIMESTAMP, num NUMERIC, u BIGINT, "
                 "g UUID, iv INTERVAL);");

    // Zero rows check
    EXPECT_EQ(conn->insert_many_batch("INSERT INTO pg_tmp_batch (id, v) VALUES (?, ?)", {}, 2, 0), 0);

    // Insert with various types including escape chars, guid, interval
    SqlInterval iv_val;
    iv_val.type = IntervalType::Day;
    iv_val.days = 5;

    std::vector<BoundValue> params = {
        int64_t{1}, std::string{"hello\tworld\nline2\r\\escaped"}, 12.34, true,
        std::vector<uint8_t>{0xaa, 0xbb}, SqlDate(2023, 5, 1), SqlTime(8, 0, 0),
        SqlTimestamp(2023, 5, 1, 8, 0, 0), SqlNumeric("100.50"), uint64_t{12345},
        SqlGuid("12345678-1234-5678-1234-567812345678"), iv_val,

        int64_t{2}, std::monostate{}, 56.78, false,
        std::vector<uint8_t>{}, SqlDate(2023, 5, 2), SqlTime(9, 15, 0),
        SqlTimestamp(2023, 5, 2, 9, 15, 0), SqlNumeric("200.75"), uint64_t{67890},
        SqlGuid("87654321-4321-8765-4321-876543210987"), iv_val,

        int64_t{3}, std::wstring{L"wide_str"}, 90.12, true,
        std::vector<uint8_t>{1, 2, 3}, SqlDate(2023, 5, 3), SqlTime(10, 30, 0),
        SqlTimestamp(2023, 5, 3, 10, 30, 0), SqlNumeric("300.00"), uint64_t{99999},
        SqlGuid("00000000-0000-0000-0000-000000000000"), iv_val
    };

    size_t count = conn->insert_many_batch(
        "INSERT INTO pg_tmp_batch (id, v, f, b, bl, d, t, ts, num, u, g, iv) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
        params, 12, 3
    );
    EXPECT_EQ(count, 3);

    auto r = conn->execute_query_direct("SELECT v FROM pg_tmp_batch WHERE id = 1;");
    EXPECT_TRUE(r->next());
    EXPECT_EQ(r->get_string(0), "hello\tworld\nline2\r\\escaped");

    // Large batch to cross buffer >= 32768 flush threshold
    std::vector<BoundValue> large_params;
    large_params.reserve(500 * 2);
    std::string padding(80, 'z');
    for (int i = 0; i < 500; ++i) {
        large_params.push_back(int64_t{1000 + i});
        large_params.push_back(padding + std::to_string(i));
    }
    size_t large_count = conn->insert_many_batch(
        "INSERT INTO pg_tmp_batch (id, v) VALUES (?, ?)",
        large_params, 2, 500
    );
    EXPECT_EQ(large_count, 500);
}

TEST(PostgresIntegrationTest, InsertManyBatchFallbackLoop) {
    if (!PostgresParams::is_enabled()) GTEST_SKIP() << PostgresParams::skip_reason();

    auto conn = make_connection<postgres>(PostgresParams::connection_string());
    conn->open();

    conn->execute("CREATE TEMP TABLE pg_tmp_fallback (id INT, v TEXT);");

    std::vector<BoundValue> params = {
        int64_t{10}, std::string{"fall1"},
        int64_t{20}, std::string{"fall2"}
    };
    // Non-standard INSERT (SELECT instead of VALUES) uses prepared statement fallback loop
    size_t affected = conn->insert_many_batch(
        "INSERT INTO pg_tmp_fallback (id, v) SELECT ?::int, ?::text",
        params, 2, 2
    );
    EXPECT_EQ(affected, 2);
}

TEST(PostgresIntegrationTest, PreparedStatementFeatures) {
    if (!PostgresParams::is_enabled()) GTEST_SKIP() << PostgresParams::skip_reason();

    auto conn = make_connection<postgres>(PostgresParams::connection_string());
    conn->open();

    // Query with single quotes and double quotes containing '?' to test placeholder conversion
    auto stmt = conn->prepare(
        "SELECT ?::int AS id, ?::text AS v, ?::float8 AS f, ?::boolean AS b, "
        "?::bytea AS bl, ?::numeric AS num, ?::date AS d, ?::time AS t, "
        "?::timestamp AS ts, ?::bigint AS u, ?::text AS w, 'str?const' AS literal, "
        "\"col?alias\" AS alias FROM (SELECT 1 AS \"col?alias\") sub;"
    );

    stmt->set_timeout(10);
    std::stop_source stop_src;
    stmt->set_stop_token(stop_src.get_token());
    stop_src.request_stop(); // triggers cancellation callback
    stmt->cancel();

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

    auto reader = stmt->execute_query();
    EXPECT_TRUE(reader->next());
    EXPECT_EQ(reader->get_int64(0), 100);
    EXPECT_EQ(reader->get_string(1), "prep_val");
    EXPECT_NEAR(reader->get_double(2), 45.67, 1e-4);
    EXPECT_TRUE(reader->get_bool(3));
    EXPECT_EQ(reader->get_blob(4), (std::vector<uint8_t>{0xde, 0xad}));
    EXPECT_EQ(reader->get_uint64(9), 7777777777ULL);
    EXPECT_EQ(reader->get_wstring(10), L"wide_prep");
    EXPECT_EQ(reader->get_string(11), "str?const");
    EXPECT_EQ(reader->get_int64(12), 1);

    stmt->reset();

    // Prepared statement non-query
    conn->execute("CREATE TEMP TABLE pg_tmp_prep(id INT, v TEXT);");
    auto ins_stmt = conn->prepare("INSERT INTO pg_tmp_prep VALUES (?, ?);");
    ins_stmt->bind(0, int64_t{1});
    ins_stmt->bind(1, std::string{"one"});
    size_t aff = ins_stmt->execute_non_query();
    EXPECT_EQ(aff, 1);

    // Prepared statement error cases
    EXPECT_THROW({
        auto bad_stmt = conn->prepare("SYNTAX ERROR NOT A QUERY");
        bad_stmt->execute_query();
    }, DbException);

    EXPECT_THROW({
        auto bad_stmt = conn->prepare("SYNTAX ERROR NOT A QUERY");
        bad_stmt->execute_non_query();
    }, DbException);

    // Runtime execution error (division by zero)
    auto div_stmt = conn->prepare("SELECT 1 / ?::int;");
    div_stmt->bind(0, int64_t{0});
    EXPECT_THROW(div_stmt->execute_query(), DbException);

    // Runtime non-query error
    auto bad_exec = conn->prepare("INSERT INTO non_existent_tbl_xyz VALUES (?);");
    bad_exec->bind(0, int64_t{1});
    EXPECT_THROW(bad_exec->execute_non_query(), DbException);

    // Prepared statement with SqlGuid and SqlInterval
    auto g_stmt = conn->prepare("SELECT ?::uuid, ?::interval;");
    g_stmt->bind(0, SqlGuid("12345678-1234-5678-1234-567812345678"));
    SqlInterval iv_q;
    iv_q.type = IntervalType::Day;
    iv_q.days = 3;
    g_stmt->bind(1, iv_q);
    auto g_r = g_stmt->execute_query();
    EXPECT_TRUE(g_r->next());
    EXPECT_EQ(g_r->get_guid(0).to_string(), "12345678-1234-5678-1234-567812345678");
    EXPECT_EQ(g_r->get_interval(1).days, 3);

    // Direct query & non-query & execute error paths
    EXPECT_THROW(conn->execute_query_direct("SELECT * FROM non_existent_table_99999;"), DbException);
    EXPECT_THROW(conn->execute_non_query_direct("UPDATE non_existent_table_99999 SET x = 1;"), DbException);
    EXPECT_THROW(conn->execute("INVALID SQL SYNTAX;"), DbException);
}

TEST(PostgresIntegrationTest, ConnectionStringAndClosedErrors) {
    // Closed connection throwing DbException
    {
        auto conn = make_connection<postgres>("host=localhost");
        EXPECT_FALSE(conn->is_open());
        EXPECT_THROW(conn->prepare("SELECT 1;"), DbException);
        EXPECT_THROW(conn->execute("SELECT 1;"), DbException);
        EXPECT_THROW(conn->execute_query_direct("SELECT 1;"), DbException);
        EXPECT_THROW(conn->execute_non_query_direct("SELECT 1;"), DbException);
        EXPECT_THROW(conn->insert_many_batch("INSERT INTO t VALUES (?)", {int64_t{1}}, 1, 1), DbException);
    }

    // Invalid connection throwing DbException
    {
        auto conn = make_connection<postgres>("host=127.0.0.1 port=19999 dbname=invalid user=invalid connect_timeout=1");
        EXPECT_THROW(conn->open(), DbException);
    }

    if (!PostgresParams::is_enabled()) {
        GTEST_SKIP() << PostgresParams::skip_reason();
    }

    // Empty connection string (falls back to env or default)
    {
        auto conn = make_connection<postgres>("");
        EXPECT_NO_THROW(conn->open());
        EXPECT_TRUE(conn->is_open());
        conn->close();
        EXPECT_FALSE(conn->is_open());
    }

    // ODBC-style connection string
    {
        std::string odbc_str = "Server=localhost;Port=5432;Database=cppdb;Uid=cppdb;Pwd=cppdb_password;sslmode=disable;";
        auto conn = make_connection<postgres>(odbc_str);
        EXPECT_NO_THROW(conn->open());
        EXPECT_TRUE(conn->is_open());
    }

    // ODBC-style with curly braces and case variations
    {
        std::string odbc_str2 = "Host=localhost;Port=5432;Db=cppdb;User=cppdb;Password={cppdb_password};";
        auto conn = make_connection<postgres>(odbc_str2);
        EXPECT_NO_THROW(conn->open());
        EXPECT_TRUE(conn->is_open());
    }

    // Single DSN word without '='
    {
        auto conn = make_connection<postgres>("PostgreSQL35W");
        EXPECT_NO_THROW(conn->open());
        EXPECT_TRUE(conn->is_open());
    }

    // postgresql:// and postgres:// URI formats
    {
        auto conn1 = make_connection<postgres>("postgresql://cppdb:cppdb_password@localhost:5432/cppdb");
        EXPECT_NO_THROW(conn1->open());
        EXPECT_TRUE(conn1->is_open());

        auto conn2 = make_connection<postgres>("postgres://cppdb:cppdb_password@localhost:5432/cppdb");
        EXPECT_NO_THROW(conn2->open());
        EXPECT_TRUE(conn2->is_open());
    }
}

#endif

