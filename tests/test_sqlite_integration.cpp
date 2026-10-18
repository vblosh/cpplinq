#include "integration_test_suite.h"
#include "dialect/sqlite_dialect.h"

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

struct NullableDetail {
    std::optional<int> score;
    std::optional<std::string> note;

    bool operator==(const NullableDetail& other) const = default;
};

inline const auto nullable_details_table = table<NullableDetail>(
    "test_nullable_details",
    column("score", &NullableDetail::score),
    column("note", &NullableDetail::note)
);

struct DuplicateTag {
    int code = 0;
    std::string label;

    bool operator==(const DuplicateTag& other) const = default;
};

inline const auto duplicate_tags_table = table<DuplicateTag>(
    "test_duplicate_tags",
    column("code", &DuplicateTag::code, not_null),
    column("label", &DuplicateTag::label, not_null)
);

struct NaturalKeyUser {
    std::string username;
    std::string email;
    int points = 0;

    bool operator==(const NaturalKeyUser& other) const = default;
};

inline const auto natural_key_users_table = table<NaturalKeyUser>(
    "test_natural_key_users",
    column("username", &NaturalKeyUser::username, primary_key),
    column("email", &NaturalKeyUser::email),
    column("points", &NaturalKeyUser::points)
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
                .where(orders_table["amount"] > 30.0)
                .order_by_desc(orders_table["amount"])
                .limit(1)
                .offset(1);
    // Joined count ignores ORDER BY, LIMIT, and OFFSET.
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
    auto sq = q1.union_with(q2).order_by_desc(users_table["age"]).limit(1).offset(1);
    // Set-operation count ignores ORDER BY, LIMIT, and OFFSET.
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

TEST(SqliteReviewFixesTest, InsertManyBatchOutOfBoundsThrows) {
    DbContext<cpplinq::sqlite> db(":memory:");
    std::vector<BoundValue> flat_params = {int64_t(1), std::string("Alice")};
    EXPECT_THROW(db.connection().insert_many_batch("INSERT INTO test VALUES (?, ?)", flat_params, 2, 2), DbException);
}

TEST(SqliteReviewFixesTest, InsertManyBatchExecution) {
    DbContext<cpplinq::sqlite> db(":memory:");
    db.connection().execute_non_query_direct("CREATE TABLE batch_test (id INT, name TEXT)");
    std::vector<BoundValue> flat_params = {
        int64_t(1), std::string("Alice"),
        int64_t(2), std::string("Bob"),
        int64_t(3), std::string("Charlie")
    };
    size_t affected = db.connection().insert_many_batch("INSERT INTO batch_test VALUES (?, ?)", flat_params, 2, 3);
    EXPECT_EQ(affected, 3u);

    auto reader = db.connection().execute_query_direct("SELECT COUNT(*) FROM batch_test");
    ASSERT_TRUE(reader->next());
    EXPECT_EQ(reader->get_int64(0), 3);
}

TEST(SqliteReviewFixesTest, InsertManyBatchOverflowGuard) {
    DbContext<cpplinq::sqlite> db(":memory:");
    std::vector<BoundValue> flat_params = {int64_t(1), std::string("Alice")};
    size_t large_row_count = SIZE_MAX / 2 + 1;
    size_t col_count = 2;
    EXPECT_THROW(db.connection().insert_many_batch("INSERT INTO test VALUES (?, ?)", flat_params, col_count, large_row_count), DbException);
}

TEST(SqliteReviewFixesTest, DateAddDaysNegativeAndPositiveExecution) {
    DbContext<cpplinq::sqlite> db(":memory:");
    SqliteDialect dialect;
    std::string sql_neg = "SELECT " + dialect.date_add_days_func("'2026-10-16'", "-7") + " AS d";
    auto reader_neg = db.connection().execute_query_direct(sql_neg);
    ASSERT_TRUE(reader_neg->next());
    EXPECT_FALSE(reader_neg->is_null(0));
    EXPECT_EQ(reader_neg->get_string(0), "2026-10-09");

    std::string sql_pos = "SELECT " + dialect.date_add_days_func("'2026-10-16'", "7") + " AS d";
    auto reader_pos = db.connection().execute_query_direct(sql_pos);
    ASSERT_TRUE(reader_pos->next());
    EXPECT_FALSE(reader_pos->is_null(0));
    EXPECT_EQ(reader_pos->get_string(0), "2026-10-23");
}

TEST(SqliteReviewFixesTest, OffsetWithoutLimitExecutesValidSql) {
    DbContext<cpplinq::sqlite> db(":memory:");
    db.connection().execute_non_query_direct("CREATE TABLE test_offset (id INT)");
    db.connection().execute_non_query_direct("INSERT INTO test_offset VALUES (1), (2), (3)");

    SqliteDialect dialect;
    std::string sql = "SELECT id FROM test_offset" + dialect.limit_offset(std::nullopt, 1);
    EXPECT_EQ(sql, "SELECT id FROM test_offset LIMIT -1 OFFSET 1");

    auto reader = db.connection().execute_query_direct(sql);
    ASSERT_TRUE(reader->next());
    EXPECT_EQ(reader->get_int64(0), 2);
    ASSERT_TRUE(reader->next());
    EXPECT_EQ(reader->get_int64(0), 3);
    EXPECT_FALSE(reader->next());
}

namespace {

// Build a TableDef whose name matches a CTE, reusing the entity and columns.
template <typename Entity, typename... Cols>
auto renamed_table(std::string_view name, const TableDef<Entity, Cols...>& src) {
    return std::apply(
        [name](auto... cols) { return table<Entity>(name, cols...); },
        src.columns);
}

} // namespace

TEST(SqliteReviewFixesTest, FluentCteQueryExecutes) {
    DbContext<cpplinq::sqlite> db(":memory:");
    db.ensure_table(users_table);
    db.insert(users_table, User{0, "Alice", {}, 30});
    db.insert(users_table, User{0, "Bob", {}, 17});
    db.insert(users_table, User{0, "Cara", {}, 42});

    auto adults = db.from(users_table).where(users_table["age"] >= 18);
    auto adult_users = renamed_table("adult_users", users_table);

    auto result = db.from(adult_users)
                    .with_cte("adult_users", adults)
                    .order_by(adult_users["age"])
                    .to_list();

    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0].name, "Alice");
    EXPECT_EQ(result[1].name, "Cara");
}

TEST(SqliteReviewFixesTest, FluentCteDropsSourceLimitAndOrder) {
    DbContext<cpplinq::sqlite> db(":memory:");
    db.ensure_table(users_table);
    for (int i = 1; i <= 5; ++i) {
        db.insert(users_table, User{0, "User" + std::to_string(i), {}, 20 + i});
    }

    // LIMIT 1 on the CTE source is not preserved by SubqueryExpr, so the CTE
    // materializes all five rows.
    auto limited = db.from(users_table)
                     .order_by_desc(users_table["age"])
                     .limit(1);

    auto all_users = std::apply(
        [](auto... cols) { return table<User>("all_users", cols...); },
        users_table.columns);

    auto result = db.from(all_users)
                    .with_cte("all_users", limited)
                    .to_list();

    EXPECT_EQ(result.size(), 5u);
}

TEST(SqliteReviewFixesTest, FluentMultipleCtesBindParametersInOrder) {
    DbContext<cpplinq::sqlite> db(":memory:");
    db.ensure_table(users_table);
    db.ensure_table(orders_table);
    int alice = static_cast<int>(db.insert(users_table, User{0, "Alice", {}, 30}));
    int bob = static_cast<int>(db.insert(users_table, User{0, "Bob", {}, 40}));
    db.insert(orders_table, Order{0, alice, 150.0});
    db.insert(orders_table, Order{0, bob, 50.0});

    auto q1 = db.from(users_table).where(users_table["age"] >= 35);       // CTE param 1
    auto q2 = db.from(orders_table).where(orders_table["amount"] >= 100); // CTE param 2

    auto rich_users = std::apply(
        [](auto... cols) { return table<User>("rich_users", cols...); },
        users_table.columns);

    auto result = db.from(rich_users)
                    .with_cte("rich_users", q1)
                    .with_cte("big_orders", q2)
                    .where(rich_users["age"] >= 35) // outer param 3
                    .to_list();

    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].name, "Bob");
}

TEST(SqliteReviewFixesTest, ExistsSubqueryExecutes) {
    DbContext<cpplinq::sqlite> db(":memory:");
    db.ensure_table(users_table);
    db.ensure_table(orders_table);
    int alice = static_cast<int>(db.insert(users_table, User{0, "Alice", {}, 30}));
    db.insert(users_table, User{0, "Bob", {}, 40});
    db.insert(orders_table, Order{0, alice, 150.0});

    auto any_order = db.from(orders_table)
                       .where(orders_table["user_id"] == users_table["id"]);

    auto buyers = db.from(users_table)
                    .where(exists(any_order.as_subquery()))
                    .to_list();
    ASSERT_EQ(buyers.size(), 1u);
    EXPECT_EQ(buyers[0].name, "Alice");

    auto non_buyers = db.from(users_table)
                         .where(not_exists(any_order.as_subquery()))
                         .to_list();
    ASSERT_EQ(non_buyers.size(), 1u);
    EXPECT_EQ(non_buyers[0].name, "Bob");
}

TEST(SqliteReviewFixesTest, ExistsAcceptsQueryBuilderDirectly) {
    DbContext<cpplinq::sqlite> db(":memory:");
    db.ensure_table(users_table);
    db.ensure_table(orders_table);
    int alice = static_cast<int>(db.insert(users_table, User{0, "Alice", {}, 30}));
    db.insert(users_table, User{0, "Bob", {}, 40});
    db.insert(orders_table, Order{0, alice, 150.0});

    auto any_order = db.from(orders_table)
                       .where(orders_table["user_id"] == users_table["id"]);

    auto buyers = db.from(users_table)
                    .where(exists(any_order))
                    .to_list();
    ASSERT_EQ(buyers.size(), 1u);
    EXPECT_EQ(buyers[0].name, "Alice");

    auto non_buyers = db.from(users_table)
                         .where(not_exists(any_order))
                         .to_list();
    ASSERT_EQ(non_buyers.size(), 1u);
    EXPECT_EQ(non_buyers[0].name, "Bob");
}

TEST(SqliteReviewFixesTest, InAndNotInSubqueries) {
    DbContext<cpplinq::sqlite> db(":memory:");
    db.ensure_table(users_table);
    db.ensure_table(orders_table);
    int alice = static_cast<int>(db.insert(users_table, User{0, "Alice", {}, 30}));
    int bob = static_cast<int>(db.insert(users_table, User{0, "Bob", {}, 40}));
    db.insert(users_table, User{0, "Cara", {}, 50});
    db.insert(orders_table, Order{0, alice, 150.0});
    db.insert(orders_table, Order{0, bob, 50.0});

    auto big_spender_ids = db.from(orders_table)
                              .where(orders_table["amount"] >= 100.0)
                              .as_subquery(orders_table["user_id"]);

    auto included = db.from(users_table)
                        .where(users_table["id"].in(big_spender_ids))
                        .to_list();
    ASSERT_EQ(included.size(), 1u);
    EXPECT_EQ(included[0].name, "Alice");

    auto excluded = db.from(users_table)
                        .where(users_table["id"].not_in(big_spender_ids))
                        .order_by(users_table["name"])
                        .to_list();
    ASSERT_EQ(excluded.size(), 2u);
    EXPECT_EQ(excluded[0].name, "Bob");
    EXPECT_EQ(excluded[1].name, "Cara");
}

TEST(SqliteReviewFixesTest, CteSurvivesFirstButIsDroppedByScalarTerminals) {
    DbContext<cpplinq::sqlite> db(":memory:");
    db.ensure_table(users_table);
    db.insert(users_table, User{0, "Alice", {}, 30});
    db.insert(users_table, User{0, "Bob", {}, 17});
    db.insert(users_table, User{0, "Cara", {}, 42});

    auto adults = db.from(users_table).where(users_table["age"] >= 18);
    auto cte_users = renamed_table("cte_only_users", users_table);
    auto query = db.from(cte_users)
                    .with_cte("cte_only_users", adults)
                    .order_by(cte_users["age"]);

    auto first = query.first();
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->name, "Alice");

    // Scalar terminals omit the CTE list, so the CTE-only FROM target cannot
    // be resolved once the WITH clause is gone.
    EXPECT_THROW(query.count(), DbException);
    EXPECT_THROW(query.count_distinct(cte_users["age"]), DbException);
    EXPECT_THROW(query.avg(cte_users["age"]), DbException);
    EXPECT_THROW(query.sum(cte_users["age"]), DbException);
    EXPECT_THROW(query.min_val(cte_users["age"]), DbException);
    EXPECT_THROW(query.max_val(cte_users["age"]), DbException);
    EXPECT_THROW(query.exists(), DbException);
}

TEST(SqliteReviewFixesTest, CountIgnoresDistinctGroupByHavingOrderLimitOffset) {
    DbContext<cpplinq::sqlite> db(":memory:");
    db.ensure_table(duplicate_tags_table);
    db.insert(duplicate_tags_table, DuplicateTag{1, "a"});
    db.insert(duplicate_tags_table, DuplicateTag{1, "a"});
    db.insert(duplicate_tags_table, DuplicateTag{2, "b"});

    auto distinct_rows = db.from(duplicate_tags_table).distinct().to_list();
    EXPECT_EQ(distinct_rows.size(), 2u);
    EXPECT_EQ(db.from(duplicate_tags_table).distinct().count(), 3u);

    EXPECT_EQ(db.from(duplicate_tags_table).count_distinct(duplicate_tags_table["code"]), 2u);
    EXPECT_EQ(db.from(duplicate_tags_table)
                  .group_by(duplicate_tags_table["code"], duplicate_tags_table["label"])
                  .having(duplicate_tags_table["code"] > 100)
                  .count(),
              3u);
    EXPECT_EQ(db.from(duplicate_tags_table)
                  .order_by(duplicate_tags_table["code"])
                  .limit(1)
                  .offset(1)
                  .count(),
              3u);
}

TEST(SqliteReviewFixesTest, LeftJoinAllNullMatchedRowMapsToNullopt) {
    DbContext<cpplinq::sqlite> db(":memory:");
    db.ensure_table(users_table);
    db.ensure_table(nullable_details_table);
    db.insert(users_table, User{0, "Alice", {}, 30});
    db.insert(nullable_details_table, NullableDetail{std::nullopt, std::nullopt});
    db.insert(nullable_details_table, NullableDetail{7, "present"});

    // The join condition intentionally does not reference the joined table,
    // so the all-NULL row is still a matched row.
    auto rows = db.from(users_table)
                  .left_join(nullable_details_table)
                  .on(users_table["id"] >= 0)
                  .to_list();

    ASSERT_EQ(rows.size(), 2u);
    size_t null_rows = 0;
    size_t present_rows = 0;
    for (const auto& [user, detail] : rows) {
        EXPECT_EQ(user.name, "Alice");
        if (detail.has_value()) {
            ++present_rows;
            EXPECT_EQ(detail->score, 7);
            EXPECT_EQ(detail->note, "present");
        } else {
            ++null_rows;
        }
    }
    EXPECT_EQ(null_rows, 1u);
    EXPECT_EQ(present_rows, 1u);
}

TEST(SqliteReviewFixesTest, SetOpFirstAndCountIgnorePaginationAndOrder) {
    DbContext<cpplinq::sqlite> db(":memory:");
    db.ensure_table(users_table);
    db.insert(users_table, User{0, "Alice", {}, 20});
    db.insert(users_table, User{0, "Bob", {}, 30});
    db.insert(users_table, User{0, "Cara", {}, 40});
    db.insert(users_table, User{0, "Dan", {}, 50});

    auto young = db.from(users_table).where(users_table["age"] <= 20);
    auto older = db.from(users_table).where(users_table["age"] >= 30);
    auto combined = young.union_all(older)
                        .order_by_desc(users_table["age"])
                        .limit(2)
                        .offset(1);

    auto rows = combined.to_list();
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0].name, "Cara");
    EXPECT_EQ(rows[1].name, "Bob");

    auto first = combined.first();
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->name, "Cara");

    // The count terminal drops ORDER BY, LIMIT, and OFFSET.
    EXPECT_EQ(combined.count(), 4u);
}

TEST(SqliteReviewFixesTest, InsertManyDoesNotPopulateGeneratedIds) {
    DbContext<cpplinq::sqlite> db(":memory:");
    db.ensure_table(users_table);

    std::vector<User> batch = {
        User{0, "BatchOne", "one@test.com", 20},
        User{0, "BatchTwo", "two@test.com", 21}
    };
    db.insert_many(users_table, batch);

    EXPECT_EQ(batch[0].id, 0);
    EXPECT_EQ(batch[1].id, 0);

    auto stored = db.from(users_table).order_by(users_table["name"]).to_list();
    ASSERT_EQ(stored.size(), 2u);
    EXPECT_GT(stored[0].id, 0);
    EXPECT_GT(stored[1].id, 0);
    EXPECT_NE(stored[0].id, stored[1].id);

    // The unchanged id=0 values do not match the generated rows.
    for (auto& user : batch) {
        user.age = 99;
    }
    EXPECT_EQ(db.update_many(users_table, batch), 0u);

    auto reloaded = db.from(users_table).order_by(users_table["name"]).to_list();
    ASSERT_EQ(reloaded.size(), 2u);
    EXPECT_EQ(reloaded[0].age, 20);
    EXPECT_EQ(reloaded[1].age, 21);
}

TEST(SqliteReviewFixesTest, CteSurvivesStreamAndPrepare) {
    DbContext<cpplinq::sqlite> db(":memory:");
    db.ensure_table(users_table);
    db.insert(users_table, User{0, "Alice", {}, 30});
    db.insert(users_table, User{0, "Bob", {}, 17});
    db.insert(users_table, User{0, "Cara", {}, 42});

    auto adults = db.from(users_table).where(users_table["age"] >= 18);
    auto adult_users = renamed_table("adult_users_stream", users_table);
    auto query = db.from(adult_users)
                    .with_cte("adult_users_stream", adults)
                    .order_by(adult_users["age"]);

    auto prepared = query.prepare();
    auto prepared_rows = prepared.execute();
    ASSERT_EQ(prepared_rows.size(), 2u);
    EXPECT_EQ(prepared_rows[0].name, "Alice");
    EXPECT_EQ(prepared_rows[1].name, "Cara");

    std::vector<std::string> streamed_names;
    for (const auto& user : query.stream()) {
        streamed_names.push_back(user.name);
    }
    ASSERT_EQ(streamed_names.size(), 2u);
    EXPECT_EQ(streamed_names[0], "Alice");
    EXPECT_EQ(streamed_names[1], "Cara");
}

TEST(SqliteReviewFixesTest, UpsertDefaultsConflictColumnsToPrimaryKey) {
    DbContext<cpplinq::sqlite> db(":memory:");
    db.ensure_table(natural_key_users_table);

    // No explicit conflict list: the username primary key is used.
    db.upsert(natural_key_users_table, NaturalKeyUser{"alice", "alice@old.test", 10});
    db.upsert(natural_key_users_table, NaturalKeyUser{"alice", "alice@new.test", 25});
    db.upsert(natural_key_users_table, NaturalKeyUser{"bob", "bob@test", 5});

    auto rows = db.from(natural_key_users_table)
                    .order_by(natural_key_users_table["username"])
                    .to_list();
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0].username, "alice");
    EXPECT_EQ(rows[0].email, "alice@new.test");
    EXPECT_EQ(rows[0].points, 25);
    EXPECT_EQ(rows[1].username, "bob");
    EXPECT_EQ(rows[1].points, 5);
}

TEST(SqliteReviewFixesTest, BatchOperationsWithoutPrimaryKeyThrow) {
    DbContext<cpplinq::sqlite> db(":memory:");
    db.ensure_table(duplicate_tags_table);

    EXPECT_THROW(db.delete_many(duplicate_tags_table, std::vector<int>{1}), DbException);
    EXPECT_THROW(db.update_many(duplicate_tags_table, std::vector<DuplicateTag>{{1, "a"}}), DbException);
}

TEST(SqliteReviewFixesTest, SetOperationsExecute) {
    DbContext<cpplinq::sqlite> db(":memory:");
    db.ensure_table(users_table);
    db.insert(users_table, User{0, "Alice", {}, 25});
    db.insert(users_table, User{0, "Bob", {}, 35});
    db.insert(users_table, User{0, "Cara", {}, 42});
    db.insert(users_table, User{0, "Dan", {}, 19});

    auto young = db.from(users_table).where(users_table["age"] < 30);
    auto older = db.from(users_table).where(users_table["age"] >= 40);

    auto combined = young.union_all(older).order_by(users_table["age"]).to_list();
    ASSERT_EQ(combined.size(), 3u);
    EXPECT_EQ(combined[0].name, "Dan");
    EXPECT_EQ(combined[1].name, "Alice");
    EXPECT_EQ(combined[2].name, "Cara");

    auto everyone = db.from(users_table).where(users_table["age"] >= 18);
    auto common = young.intersect(everyone).to_list();
    EXPECT_EQ(common.size(), 2u);

    auto diff = everyone.except_from(young).order_by(users_table["age"]).to_list();
    ASSERT_EQ(diff.size(), 2u);
    EXPECT_EQ(diff[0].name, "Bob");
    EXPECT_EQ(diff[1].name, "Cara");
}

TEST(SqliteIntegrationCoverageTest, DeleteManyEdgeCasesAndValidation) {
    DbContext<cpplinq::sqlite> db(":memory:");

    // On table without primary key -> verify throws DbException("delete_many requires a primary key column")
    db.ensure_table(duplicate_tags_table);
    try {
        db.delete_many(duplicate_tags_table, std::vector<int>{1});
        FAIL() << "Expected DbException";
    } catch (const DbException& ex) {
        EXPECT_STREQ(ex.what(), "delete_many requires a primary key column");
    }

    // On table with composite primary key -> verify throws DbException("delete_many does not support composite primary keys")
    db.ensure_table(user_roles_table);
    try {
        db.delete_many(user_roles_table, std::vector<int>{1});
        FAIL() << "Expected DbException";
    } catch (const DbException& ex) {
        EXPECT_STREQ(ex.what(), "delete_many does not support composite primary keys");
    }

    // On empty vector of IDs -> returns 0
    EXPECT_EQ(db.delete_many(users_table, std::vector<int>{}), 0u);

    // On vector of IDs with chunking (e.g. chunk_size = 2, 5 IDs deleted)
    db.ensure_table(users_table);
    std::vector<int> ids;
    for (int i = 0; i < 5; ++i) {
        ids.push_back(static_cast<int>(db.insert(users_table, User{0, "ChunkUser" + std::to_string(i), {}, 20 + i})));
    }
    EXPECT_EQ(db.from(users_table).count(), 5u);

    size_t deleted = db.delete_many(users_table, ids, 2);
    EXPECT_EQ(deleted, 5u);
    EXPECT_EQ(db.from(users_table).count(), 0u);

    // Delete many fallback with other PK types: string, unsigned, double, bool
    db.ensure_table(natural_key_users_table);
    db.insert(natural_key_users_table, NaturalKeyUser{"user_a", "a@test.com", 1});
    db.insert(natural_key_users_table, NaturalKeyUser{"user_b", "b@test.com", 2});
    size_t del_str = db.delete_many(natural_key_users_table, std::vector<std::string>{"user_a", "user_b"}, 1);
    EXPECT_EQ(del_str, 2u);

    EXPECT_EQ(db.delete_many(users_table, std::vector<unsigned int>{99u, 100u}, 1), 0u);
    EXPECT_EQ(db.delete_many(users_table, std::vector<double>{99.0, 100.0}, 1), 0u);
    EXPECT_EQ(db.delete_many(users_table, std::vector<bool>{true}, 1), 0u);
}

TEST(SqliteIntegrationCoverageTest, UpdateManyEdgeCasesAndChunking) {
    DbContext<cpplinq::sqlite> db(":memory:");

    // On table without primary key -> verify throws DbException("update_many requires a primary key column")
    db.ensure_table(duplicate_tags_table);
    try {
        db.update_many(duplicate_tags_table, std::vector<DuplicateTag>{{1, "val"}});
        FAIL() << "Expected DbException";
    } catch (const DbException& ex) {
        EXPECT_STREQ(ex.what(), "update_many requires a primary key column");
    }

    // On empty vector of entities -> returns 0
    EXPECT_EQ(db.update_many(users_table, std::vector<User>{}), 0u);

    // On multiple entities with custom chunk_size
    db.ensure_table(users_table);
    std::vector<User> batch;
    for (int i = 0; i < 4; ++i) {
        int id = static_cast<int>(db.insert(users_table, User{0, "Orig" + std::to_string(i), {}, 20 + i}));
        batch.push_back(User{id, "Updated" + std::to_string(i), "up@test.com", 30 + i});
    }

    size_t updated = db.update_many(users_table, batch, 2);
    EXPECT_EQ(updated, 4u);

    auto list = db.from(users_table).order_by(users_table["id"]).to_list();
    ASSERT_EQ(list.size(), 4u);
    EXPECT_EQ(list[0].name, "Updated0");
    EXPECT_EQ(list[3].name, "Updated3");
}

TEST(SqliteIntegrationCoverageTest, InsertManyEdgeCasesAndChunking) {
    DbContext<cpplinq::sqlite> db(":memory:");
    db.ensure_table(users_table);

    // On empty vector of entities -> returns 0
    db.insert_many(users_table, std::vector<User>{});
    db.insert_many(users_table, std::vector<User>{}, 2);
    EXPECT_EQ(db.from(users_table).count(), 0u);

    // On entities with custom chunk_size
    std::vector<User> batch;
    for (int i = 0; i < 5; ++i) {
        batch.push_back(User{0, "Batch" + std::to_string(i), {}, 20 + i});
    }
    db.insert_many(users_table, batch, 2);
    EXPECT_EQ(db.from(users_table).count(), 5u);
}

TEST(SqliteIntegrationCoverageTest, InsertNaturalPrimaryKey) {
    DbContext<cpplinq::sqlite> db(":memory:");
    db.ensure_table(natural_key_users_table);

    // Insert on table without auto-increment column (natural primary key)
    int64_t id = db.insert(natural_key_users_table, NaturalKeyUser{"nat_alice", "alice@example.com", 100});
    EXPECT_EQ(id, 0);

    auto user = db.from(natural_key_users_table)
                  .where(natural_key_users_table["username"] == "nat_alice")
                  .first();
    ASSERT_TRUE(user.has_value());
    EXPECT_EQ(user->email, "alice@example.com");
    EXPECT_EQ(user->points, 100);
}

TEST(SqliteIntegrationCoverageTest, StreamWithExecutionOptions) {
    DbContext<cpplinq::sqlite> db(":memory:");
    db.ensure_table(users_table);
    db.insert(users_table, User{0, "StreamUser1", {}, 20});
    db.insert(users_table, User{0, "StreamUser2", {}, 21});

    std::stop_source ss;
    ExecutionOptions opts{
        .query_timeout_seconds = 5,
        .stop_token = ss.get_token()
    };

    size_t count = 0;
    for (const auto& u : db.from(users_table).stream(opts)) {
        (void)u;
        ++count;
    }
    EXPECT_EQ(count, 2u);

    // Stream directly through IConnection
    auto row_stream = db.connection().stream("SELECT * FROM test_users", {}, opts);
    size_t row_count = 0;
    for (auto it = row_stream.begin(); it != row_stream.end(); ++it) {
        ++row_count;
    }
    EXPECT_EQ(row_count, 2u);
}

TEST(SqliteIntegrationCoverageTest, SqliteDialectEdgeCases) {
    SqliteDialect dialect;

    // Dialect type_name for various types
    EXPECT_EQ(dialect.type_name(SqlType::Integer), "INTEGER");
    EXPECT_EQ(dialect.type_name(SqlType::BigInt), "INTEGER");
    EXPECT_EQ(dialect.type_name(SqlType::UnsignedBigInt), "INTEGER");
    EXPECT_EQ(dialect.type_name(SqlType::Real), "REAL");
    EXPECT_EQ(dialect.type_name(SqlType::Decimal), "TEXT");
    EXPECT_EQ(dialect.type_name(SqlType::Text), "TEXT");
    EXPECT_EQ(dialect.type_name(SqlType::WString), "TEXT");
    EXPECT_EQ(dialect.type_name(SqlType::Blob), "BLOB");
    EXPECT_EQ(dialect.type_name(SqlType::Boolean), "INTEGER");
    EXPECT_EQ(dialect.type_name(SqlType::Date), "TEXT");
    EXPECT_EQ(dialect.type_name(SqlType::Time), "TEXT");
    EXPECT_EQ(dialect.type_name(SqlType::Timestamp), "TEXT");
    EXPECT_EQ(dialect.type_name(SqlType::Interval), "TEXT");
    EXPECT_EQ(dialect.type_name(SqlType::Guid), "TEXT");
    EXPECT_EQ(dialect.type_name(static_cast<SqlType>(999)), "TEXT");

    // last_insert_id_query
    EXPECT_EQ(dialect.last_insert_id_query(), "SELECT last_insert_rowid()");
    EXPECT_EQ(dialect.last_insert_id_query("my_tbl", "my_col"), "SELECT last_insert_rowid()");

    // limit_offset variants
    EXPECT_EQ(dialect.limit_offset(10, std::nullopt), " LIMIT 10");
    EXPECT_EQ(dialect.limit_offset(std::nullopt, 5), " LIMIT -1 OFFSET 5");
    EXPECT_EQ(dialect.limit_offset(10, 5), " LIMIT 10 OFFSET 5");
    EXPECT_EQ(dialect.limit_offset(std::nullopt, std::nullopt), "");
}

namespace {

class ArrayBatchSqliteConnection final : public IConnection {
public:
    explicit ArrayBatchSqliteConnection(IConnection& inner) : inner_(inner) {}
    void open() override { inner_.open(); }
    void close() override { inner_.close(); }
    bool is_open() const override { return inner_.is_open(); }
    std::unique_ptr<IPreparedStatement> prepare(std::string_view sql) override { return inner_.prepare(sql); }
    void execute(std::string_view sql) override { inner_.execute(sql); }
    std::unique_ptr<IDataReader> execute_query_direct(std::string_view sql) override { return inner_.execute_query_direct(sql); }
    size_t execute_non_query_direct(std::string_view sql) override { return inner_.execute_non_query_direct(sql); }
    void begin_transaction() override { inner_.begin_transaction(); }
    void commit() override { inner_.commit(); }
    void rollback() override { inner_.rollback(); }
    const ISqlDialect& dialect() const override { return inner_.dialect(); }
    DriverInfo info() const override { return inner_.info(); }
    DriverCapabilities capabilities() const override {
        auto caps = inner_.capabilities();
        caps.array_batch_insert = true;
        return caps;
    }
private:
    IConnection& inner_;
};

class MockNoReturningDialect : public SqliteDialect {
public:
    std::string returning_clause(std::string_view) const override { return ""; }
    std::string output_clause(std::string_view) const override { return ""; }
    bool supports_last_insert_id() const override { return true; }
    std::string last_insert_id_query(std::string_view, std::string_view) const override {
        return "SELECT last_insert_rowid()";
    }
    bool supports_truncate() const override { return true; }
};

class MockNoReturningConnection final : public IConnection {
public:
    explicit MockNoReturningConnection(IConnection& inner) : inner_(inner) {}
    void open() override { inner_.open(); }
    void close() override { inner_.close(); }
    bool is_open() const override { return inner_.is_open(); }
    std::unique_ptr<IPreparedStatement> prepare(std::string_view sql) override { return inner_.prepare(sql); }
    void execute(std::string_view sql) override {
        if (sql.find("TRUNCATE TABLE") != std::string_view::npos) {
            inner_.execute("DELETE FROM " + std::string(sql.substr(15)));
        } else {
            inner_.execute(sql);
        }
    }
    std::unique_ptr<IDataReader> execute_query_direct(std::string_view sql) override { return inner_.execute_query_direct(sql); }
    size_t execute_non_query_direct(std::string_view sql) override { return inner_.execute_non_query_direct(sql); }
    void begin_transaction() override { inner_.begin_transaction(); }
    void commit() override { inner_.commit(); }
    void rollback() override { inner_.rollback(); }
    const ISqlDialect& dialect() const override { return dialect_; }
    DriverInfo info() const override { return inner_.info(); }
    DriverCapabilities capabilities() const override { return inner_.capabilities(); }
private:
    IConnection& inner_;
    MockNoReturningDialect dialect_;
};

} // namespace

TEST(SqliteIntegrationCoverageTest, DbContextConstructorsAndArrayBatchPaths) {
    auto db_factory = cpplinq::connect<cpplinq::sqlite>(":memory:");
    db_factory.ensure_table(users_table);

    // Constructor with pointer
    DbContext<cpplinq::sqlite> db_ptr(&db_factory.connection());
    EXPECT_EQ(db_ptr.from(users_table).count(), 0u);

    // Constructor with reference
    DbContext<cpplinq::sqlite> db_ref(db_factory.connection());
    EXPECT_EQ(db_ref.from(users_table).count(), 0u);

    // upsert_many on empty vector
    EXPECT_EQ(db_ref.upsert_many(users_table, std::vector<User>{}), 0u);

    // Array batch insert paths (lines 166-184, 256-281, 360-394 of db_context.h)
    ArrayBatchSqliteConnection array_conn(db_factory.connection());
    DbContext<cpplinq::sqlite> db_batch(array_conn);

    std::vector<User> batch = {
        User{0, "ArrayUser1", "a1@test.com", 25},
        User{0, "ArrayUser2", "a2@test.com", 26},
        User{0, "ArrayUser3", "a3@test.com", 27}
    };
    db_batch.insert_many(users_table, batch, 2);
    EXPECT_EQ(db_batch.from(users_table).count(), 3u);

    auto inserted_list = db_batch.from(users_table).order_by(users_table["id"]).to_list();
    ASSERT_EQ(inserted_list.size(), 3u);
    std::vector<User> inserted(inserted_list.begin(), inserted_list.end());
    inserted[0].name = "UpdatedArray1";
    inserted[1].name = "UpdatedArray2";
    inserted[2].name = "UpdatedArray3";

    size_t updated = db_batch.update_many(users_table, inserted, 2);
    EXPECT_EQ(updated, 3u);

    // delete_many via array batch
    size_t deleted_single = db_batch.delete_many(users_table, std::vector<int>{inserted[0].id}, 1);
    EXPECT_EQ(deleted_single, 1u);

    size_t deleted_rest = db_batch.delete_many(users_table, std::vector<int>{inserted[1].id, inserted[2].id}, 2);
    EXPECT_EQ(deleted_rest, 2u);
    EXPECT_EQ(db_batch.from(users_table).count(), 0u);

    // delete_many via array batch with natural key string
    db_batch.ensure_table(natural_key_users_table);
    db_batch.insert(natural_key_users_table, NaturalKeyUser{"nat1", "n1@test.com", 1});
    db_batch.insert(natural_key_users_table, NaturalKeyUser{"nat2", "n2@test.com", 2});
    size_t del_str = db_batch.delete_many(natural_key_users_table, std::vector<std::string>{"nat1", "nat2"}, 1);
    EXPECT_EQ(del_str, 2u);

    // delete_many via array batch with uint, double, bool types
    db_batch.delete_many(users_table, std::vector<uint64_t>{10, 20}, 1);
    db_batch.delete_many(users_table, std::vector<double>{10.5, 20.5}, 1);
    db_batch.delete_many(users_table, std::vector<bool>{true, false}, 1);
}

TEST(SqliteIntegrationCoverageTest, InsertWithLastInsertIdAndTruncate) {
    DbContext<cpplinq::sqlite> raw_db(":memory:");
    raw_db.ensure_table(users_table);

    MockNoReturningConnection mock_conn(raw_db.connection());
    DbContext<cpplinq::sqlite> db(mock_conn);

    int64_t id1 = db.insert(users_table, User{0, "NoRet1", {}, 30});
    EXPECT_GT(id1, 0);

    int64_t id2 = db.insert(users_table, User{0, "NoRet2", {}, 31});
    EXPECT_GT(id2, id1);

    EXPECT_EQ(db.from(users_table).count(), 2u);

    // Truncate via supports_truncate = true (line 504 of db_context.h)
    db.truncate(users_table);
    EXPECT_EQ(db.from(users_table).count(), 0u);
}

TEST(SqliteIntegrationCoverageTest, SqliteDriverDirectMethodsCoverage) {
    auto conn = make_connection<cpplinq::sqlite>(":memory:");
    conn->open();

    // Base IConnection::execute_non_query_direct fallback
    size_t direct_res = conn->IConnection::execute_non_query_direct("CREATE TABLE direct_t(x INT, b BLOB, f REAL);");
    EXPECT_EQ(direct_res, 0u);

    // Driver info
    auto info = conn->info();
    EXPECT_EQ(info.driver_name, "SQLite");
    EXPECT_EQ(info.dbms_name, "SQLite");

    // Float and blob in get_value, and empty blob
    conn->execute("INSERT INTO direct_t VALUES (1, X'010203', 3.14), (2, X'', 2.71);");
    auto reader = conn->execute_query_direct("SELECT f, b FROM direct_t ORDER BY x ASC;");
    ASSERT_TRUE(reader->next());
    auto val_f = reader->get_value(0);
    EXPECT_TRUE(std::holds_alternative<double>(val_f));
    EXPECT_NEAR(std::get<double>(val_f), 3.14, 0.001);

    auto val_b = reader->get_value(1);
    EXPECT_TRUE(std::holds_alternative<std::vector<uint8_t>>(val_b));
    EXPECT_EQ(std::get<std::vector<uint8_t>>(val_b).size(), 3u);

    ASSERT_TRUE(reader->next());
    auto empty_blob = reader->get_blob(1);
    EXPECT_TRUE(empty_blob.empty());

    // Bind error on out-of-bounds parameter index
    auto stmt = conn->prepare("SELECT ?;");
    EXPECT_THROW(stmt->bind(999, 42), DbException);

    // Prepare syntax error
    EXPECT_THROW(conn->prepare("SYNTAX ERROR BOGUS;"), DbException);

    // Execute syntax error
    EXPECT_THROW(conn->execute("SYNTAX ERROR BOGUS;"), DbException);

    // Closed connection checks
    conn->close();
    EXPECT_FALSE(conn->is_open());
    EXPECT_THROW(conn->prepare("SELECT 1;"), DbException);
    EXPECT_THROW(conn->execute("SELECT 1;"), DbException);
    EXPECT_THROW(conn->execute_query_direct("SELECT 1;"), DbException);
}



