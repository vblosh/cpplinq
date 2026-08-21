#include "integration_test_suite.h"

struct SqliteParams {
    using backend_type = cpplinq::sqlite;
    static constexpr const char* name = "SQLite";

    static bool is_enabled() {
        return true;
    }

    static std::string skip_reason() {
        return "";
    }

    static std::string connection_string() {
        return ":memory:";
    }

    static void clean_tables(cpplinq::DbContext<backend_type>& /*db*/) {}
};

INSTANTIATE_TYPED_TEST_SUITE_P(Sqlite, IntegrationTestSuite, SqliteParams);

TEST(SqliteErrorHandlingTest, ConnectionClosedThrows) {
    auto conn = make_connection<sqlite>(":memory:");
    EXPECT_THROW(conn->execute("SELECT 1"), DbException);
    EXPECT_THROW(conn->prepare("SELECT 1"), DbException);
    EXPECT_THROW(conn->stream("SELECT 1"), DbException);
}

TEST(SqliteErrorHandlingTest, InvalidSqlSyntaxThrows) {
    auto conn = make_connection<sqlite>(":memory:");
    conn->open();
    EXPECT_THROW(conn->prepare("SELECT INVALID SYNTAX FROM @@@"), DbException);
    EXPECT_THROW(conn->execute("CREATE TABL INVALID"), DbException);
}

TEST(SqliteErrorHandlingTest, DuplicateKeyConstraintThrows) {
    auto conn = make_connection<sqlite>(":memory:");
    conn->open();
    conn->execute("CREATE TABLE test_pk (id INT PRIMARY KEY, name TEXT)");
    conn->execute("INSERT INTO test_pk VALUES (1, 'A')");
    EXPECT_THROW(conn->execute("INSERT INTO test_pk VALUES (1, 'B')"), DbException);
}

TEST(SqliteErrorHandlingTest, ExecuteNonQueryDirect) {
    auto conn = make_connection<sqlite>(":memory:");
    conn->open();
    conn->execute("CREATE TABLE test_direct (id INT, val TEXT)");
    size_t count = conn->execute_non_query_direct("INSERT INTO test_direct VALUES (1, 'A'), (2, 'B')");
    EXPECT_EQ(count, 2);
}

TEST(SqliteErrorHandlingTest, BlobAndBoolColumns) {
    auto conn = make_connection<sqlite>(":memory:");
    conn->open();
    conn->execute("CREATE TABLE test_types (id INT, flag INT, data BLOB)");
    
    auto stmt = conn->prepare("INSERT INTO test_types VALUES (?, ?, ?)");
    std::vector<uint8_t> blob = {0x01, 0x02, 0x03, 0x04};
    stmt->bind(0, int64_t(1));
    stmt->bind(1, true);
    stmt->bind(2, blob);
    stmt->execute_non_query();

    auto reader = conn->execute_query_direct("SELECT flag, data FROM test_types WHERE id = 1");
    ASSERT_TRUE(reader->next());
    EXPECT_TRUE(reader->get_bool(0));
    EXPECT_EQ(reader->get_blob(1), blob);

    // Empty blob and out-of-range
    EXPECT_TRUE(reader->get_blob(99).empty());
    EXPECT_FALSE(reader->get_bool(99));
}

