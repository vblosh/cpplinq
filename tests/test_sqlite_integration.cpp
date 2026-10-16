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

namespace {

struct UserRole {
    int user_id = 0;
    int role_id = 0;
    std::string role_name;

    bool operator==(const UserRole& other) const = default;
};

inline const auto user_roles_table = table<UserRole>(
    "test_user_roles",
    column("user_id", &UserRole::user_id, primary_key),
    column("role_id", &UserRole::role_id, primary_key),
    column("role_name", &UserRole::role_name)
);

} // namespace

TEST(SqliteReviewFixesTest, DialectSupportsTruncate) {
    DbContext<cpplinq::sqlite> db(":memory:");
    EXPECT_FALSE(db.connection().dialect().supports_truncate());

    db.ensure_table(users_table);
    db.insert(users_table, User{0, "Alice", "a@test.com", 25});
    EXPECT_EQ(db.from(users_table).count(), 1u);

    db.truncate(users_table);
    EXPECT_EQ(db.from(users_table).count(), 0u);
}

TEST(SqliteReviewFixesTest, QueryWhereChaining) {
    DbContext<cpplinq::sqlite> db(":memory:");
    db.ensure_table(users_table);
    db.insert(users_table, User{0, "Alice", "alice1@test.com", 25});
    db.insert(users_table, User{0, "Bob", "bob@test.com", 35});
    db.insert(users_table, User{0, "Alice", "alice2@test.com", 35});

    auto res = db.from(users_table)
                 .where(users_table["name"] == "Alice")
                 .where(users_table["age"] == 35)
                 .to_list();

    ASSERT_EQ(res.size(), 1u);
    EXPECT_EQ(res[0].email, "alice2@test.com");
}

TEST(SqliteReviewFixesTest, FirstPreservesLimit) {
    DbContext<cpplinq::sqlite> db(":memory:");
    db.ensure_table(users_table);
    for (int i = 0; i < 10; ++i) {
        db.insert(users_table, User{0, "User" + std::to_string(i), {}, 20 + i});
    }

    auto q = db.from(users_table).order_by(users_table["id"]).limit(5);
    auto first_val = q.first();
    ASSERT_TRUE(first_val.has_value());
    EXPECT_EQ(first_val->name, "User0");

    auto all_limited = q.to_list();
    EXPECT_EQ(all_limited.size(), 5u);
}

TEST(SqliteReviewFixesTest, JoinedAndSetOpCountExecution) {
    DbContext<cpplinq::sqlite> db(":memory:");
    db.ensure_table(users_table);
    db.ensure_table(orders_table);

    int64_t u1 = db.insert(users_table, User{0, "Alice", {}, 30});
    int64_t u2 = db.insert(users_table, User{0, "Bob", {}, 25});

    db.insert(orders_table, Order{0, static_cast<int>(u1), 50.0});
    db.insert(orders_table, Order{0, static_cast<int>(u1), 100.0});
    db.insert(orders_table, Order{0, static_cast<int>(u2), 20.0});

    // JoinedQueryBuilder count
    auto jq = db.from(users_table)
                .join(orders_table).on(users_table["id"] == orders_table["user_id"])
                .where(orders_table["amount"] > 30.0);
    EXPECT_EQ(jq.count(), 2u);

    // Joined first preserves limit
    auto jq_limited = db.from(users_table)
                        .join(orders_table).on(users_table["id"] == orders_table["user_id"])
                        .order_by(orders_table["id"])
                        .limit(2);
    auto j_first = jq_limited.first();
    EXPECT_TRUE(j_first.has_value());
    EXPECT_EQ(jq_limited.to_list().size(), 2u);

    // SetOpQueryBuilder count
    auto q1 = db.from(users_table).where(users_table["age"] <= 25);
    auto q2 = db.from(users_table).where(users_table["age"] >= 30);
    auto sq = q1.union_with(q2);
    EXPECT_EQ(sq.count(), 2u);

    // SetOp first preserves limit
    auto sq_limited = q1.union_with(q2).order_by(users_table["age"]).limit(1);
    auto sq_first = sq_limited.first();
    EXPECT_TRUE(sq_first.has_value());
    EXPECT_EQ(sq_limited.to_list().size(), 1u);
}

TEST(SqliteReviewFixesTest, EmptyColumnNameAggregatesThrow) {
    DbContext<cpplinq::sqlite> db(":memory:");
    db.ensure_table(users_table);

    ColumnHandle empty_col("", "");
    EXPECT_THROW(db.from(users_table).count_distinct(empty_col), DbException);
    EXPECT_THROW(db.from(users_table).min_val(empty_col), DbException);
    EXPECT_THROW(db.from(users_table).max_val(empty_col), DbException);
    EXPECT_THROW(db.from(users_table).sum(empty_col), DbException);
    EXPECT_THROW(db.from(users_table).avg(empty_col), DbException);
}

TEST(SqliteReviewFixesTest, DeleteManyCompositePkThrows) {
    DbContext<cpplinq::sqlite> db(":memory:");
    db.ensure_table(user_roles_table);

    EXPECT_THROW(db.delete_many(user_roles_table, std::vector<int>{1, 2}), DbException);
}

TEST(SqliteReviewFixesTest, UpdateManyCompositePk) {
    DbContext<cpplinq::sqlite> db(":memory:");
    db.ensure_table(user_roles_table);

    db.insert(user_roles_table, UserRole{1, 10, "admin"});
    db.insert(user_roles_table, UserRole{1, 20, "editor"});

    std::vector<UserRole> updates = {
        UserRole{1, 10, "superadmin"},
        UserRole{1, 20, "lead_editor"}
    };

    size_t count = db.update_many(user_roles_table, updates);
    EXPECT_EQ(count, 2u);

    auto all_roles = db.from(user_roles_table).order_by(user_roles_table["role_id"]).to_list();
    ASSERT_EQ(all_roles.size(), 2u);
    EXPECT_EQ(all_roles[0].role_name, "superadmin");
    EXPECT_EQ(all_roles[1].role_name, "lead_editor");
}

TEST(SqliteReviewFixesTest, PreparedQueryMissingDynamicArgThrows) {
    DbContext<cpplinq::sqlite> db(":memory:");
    db.ensure_table(users_table);

    auto pq = db.from(users_table)
                 .where(users_table["age"] > param<int>(0))
                 .prepare();

    EXPECT_THROW(pq.execute(), DbException);
}

TEST(SqliteReviewFixesTest, PreparedCommandMissingDynamicArgThrows) {
    DbContext<cpplinq::sqlite> db(":memory:");
    db.ensure_table(users_table);

    auto cmd = db.from(users_table)
                 .where(users_table["name"] == param<std::string>(0))
                 .prepare_update(std::vector<expr::AssignExpr>{
                     users_table["age"] = param<int>(1)
                 });

    EXPECT_THROW(cmd.execute("Alice"), DbException);
}

TEST(SqliteReviewFixesTest, TemporaryPreparedQueryStreamCancellation) {
    DbContext<cpplinq::sqlite> db(":memory:");
    db.ensure_table(users_table);
    for (int i = 0; i < 50; ++i) {
        db.insert(users_table, User{0, "User" + std::to_string(i), {}, 20 + i});
    }

    std::stop_source stop_source;
    ExecutionOptions options;
    options.stop_token = stop_source.get_token();

    // Stream directly from temporary prepared query (query object destroyed at semicolon)
    auto stream = db.from(users_table).order_by(users_table["id"]).prepare().stream_with_options(options);

    int count = 0;
    EXPECT_THROW({
        for (const auto& item : stream) {
            (void)item;
            count++;
            if (count == 5) {
                stop_source.request_stop();
            }
        }
    }, OperationCancelled);
    EXPECT_EQ(count, 5);
}

TEST(SqliteReviewFixesTest, FirstExceptionPreservesLimit) {
    DbContext<cpplinq::sqlite> db(":memory:");
    // Table not created yet
    auto q = db.from(users_table).limit(5);

    // first() will throw DbException because table does not exist
    EXPECT_THROW(q.first(), DbException);

    // Create table and verify limit remained 5 rather than 1
    db.ensure_table(users_table);
    for (int i = 0; i < 10; ++i) {
        db.insert(users_table, User{0, "User" + std::to_string(i), {}, 20 + i});
    }
    EXPECT_EQ(q.to_list().size(), 5u);
}

TEST(SqliteReviewFixesTest, DialectSupportsLastInsertId) {
    DbContext<cpplinq::sqlite> db(":memory:");
    EXPECT_TRUE(db.connection().dialect().supports_last_insert_id());
    EXPECT_EQ(db.connection().dialect().last_insert_id_query("test", "id"), "SELECT last_insert_rowid()");
}

TEST(SqliteReviewFixesTest, EmptyBlobBindingNotNull) {
    DbContext<cpplinq::sqlite> db(":memory:");
    db.connection().execute_non_query_direct("CREATE TABLE test_blob (id INTEGER PRIMARY KEY, data BLOB NOT NULL)");

    auto stmt = db.connection().prepare("INSERT INTO test_blob (id, data) VALUES (1, ?)");
    std::vector<uint8_t> empty_blob;
    stmt->bind(0, empty_blob);
    EXPECT_NO_THROW(stmt->execute_non_query());

    auto qstmt = db.connection().prepare("SELECT data FROM test_blob WHERE id = 1");
    auto reader = qstmt->execute_query();
    ASSERT_TRUE(reader->next());
    EXPECT_FALSE(reader->is_null(0));
    auto blob_result = reader->get_blob(0);
    EXPECT_TRUE(blob_result.empty());
}

TEST(SqliteReviewFixesTest, StringBooleanAndNullHandling) {
    DbContext<cpplinq::sqlite> db(":memory:");
    auto reader = db.connection().execute_query_direct(
        "SELECT 'true' AS b1, 't' AS b2, '1' AS b3, 'false' AS b4, NULL AS n1, 'True' AS b5, 'yes' AS b6, 'Y' AS b7"
    );
    ASSERT_TRUE(reader->next());
    EXPECT_TRUE(reader->get_bool(0));
    EXPECT_TRUE(reader->get_bool(1));
    EXPECT_TRUE(reader->get_bool(2));
    EXPECT_FALSE(reader->get_bool(3));
    EXPECT_FALSE(reader->get_bool(4));
    EXPECT_TRUE(reader->get_bool(5));
    EXPECT_TRUE(reader->get_bool(6));
    EXPECT_TRUE(reader->get_bool(7));

    // Test null guards in getters
    EXPECT_TRUE(reader->is_null(4));
    EXPECT_EQ(reader->get_numeric(4).to_string(), "0");
    EXPECT_EQ(reader->get_date(4).to_string(), "1970-01-01");
    EXPECT_EQ(reader->get_time(4).to_string(), "00:00:00");
    EXPECT_EQ(reader->get_timestamp(4).to_string(), "1970-01-01 00:00:00");
    EXPECT_EQ(reader->get_interval(4).to_string(), "0 00:00:00");
    EXPECT_EQ(reader->get_guid(4).to_string(), "00000000-0000-0000-0000-000000000000");
}

