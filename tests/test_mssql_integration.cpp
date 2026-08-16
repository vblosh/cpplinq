#include <gtest/gtest.h>
#include "cpplinq/cpplinq.hpp"
#include <string>
#include <optional>
#include <vector>

using namespace cpplinq;

namespace {

struct User {
    int id = 0;
    std::string name;
    std::optional<std::string> email;
    int age = 0;

    bool operator==(const User& other) const = default;
};

struct Order {
    int id = 0;
    int user_id = 0;
    double amount = 0.0;

    bool operator==(const Order& other) const = default;
};

// Define table schema for MSSQL
inline const auto users_table = table<User>(
    "test_users",
    column("id", &User::id, primary_key, auto_increment),
    column("name", &User::name, not_null),
    column("email", &User::email),
    column("age", &User::age, not_null)
);

inline const auto orders_table = table<Order>(
    "test_orders",
    column("id", &Order::id, primary_key, auto_increment),
    column("user_id", &Order::user_id, not_null),
    column("amount", &Order::amount, not_null)
);

} // namespace

// ============================================================================
// Microsoft SQL Server Integration Tests using CPPLINQ_MSSQL_ODBC
// ============================================================================

class MssqlIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        const char* env_conn = std::getenv("CPPLINQ_MSSQL_ODBC");
        if (!env_conn || env_conn[0] == '\0') {
            GTEST_SKIP() << "CPPLINQ_MSSQL_ODBC environment variable is not defined. Skipping MSSQL Server integration tests.";
            return;
        }

        conn_str_ = env_conn;
        try {
            // Connect to MSSQL using CPPLINQ_MSSQL_ODBC DSN / connection string
            db = std::make_unique<DbContext<mssql>>(conn_str_);
            
            // Clean up table if exists from prior test runs
            try {
                db->execute_raw("IF OBJECT_ID(N'test_orders', N'U') IS NOT NULL DROP TABLE [test_orders];");
                db->execute_raw("IF OBJECT_ID(N'test_users', N'U') IS NOT NULL DROP TABLE [test_users];");
            } catch (...) {}
            
            db->ensure_table(users_table);
            db->ensure_table(orders_table);
        } catch (const std::exception& e) {
            GTEST_SKIP() << "Failed to connect to MSSQL Server using CPPLINQ_MSSQL_ODBC (" << conn_str_ << "): " << e.what();
        }
    }

    void TearDown() override {
        if (db) {
            try {
                db->execute_raw("IF OBJECT_ID(N'test_orders', N'U') IS NOT NULL DROP TABLE [test_orders];");
                db->execute_raw("IF OBJECT_ID(N'test_users', N'U') IS NOT NULL DROP TABLE [test_users];");
            } catch (...) {}
        }
    }

    std::string conn_str_;
    std::unique_ptr<DbContext<mssql>> db;
};

TEST_F(MssqlIntegrationTest, EnsureTableAndInsertSingle) {
    User u1{0, "Alice", "alice@example.com", 30};
    int64_t id1 = db->insert(users_table, u1);
    EXPECT_GT(id1, 0);

    User u2{0, "Bob", std::nullopt, 25};
    int64_t id2 = db->insert(users_table, u2);
    EXPECT_GT(id2, id1);

    auto all = db->from(users_table)
                 .order_by(users_table["id"])
                 .to_vector();
    ASSERT_EQ(all.size(), 2);
    EXPECT_EQ(all[0].id, id1);
    EXPECT_EQ(all[0].name, "Alice");
    ASSERT_TRUE(all[0].email.has_value());
    EXPECT_EQ(*all[0].email, "alice@example.com");
    EXPECT_EQ(all[0].age, 30);

    EXPECT_EQ(all[1].id, id2);
    EXPECT_EQ(all[1].name, "Bob");
    EXPECT_FALSE(all[1].email.has_value());
    EXPECT_EQ(all[1].age, 25);
}

TEST_F(MssqlIntegrationTest, ConnectWithDsnPrefix) {
    auto dsn_db = cpplinq::connect<mssql>(conn_str_);
    EXPECT_TRUE(dsn_db.connection().is_open());
    EXPECT_EQ(dsn_db.from(users_table).count(), 0);
}

TEST_F(MssqlIntegrationTest, InsertMany) {
    std::vector<User> batch = {
        {0, "Charlie", "charlie@test.com", 35},
        {0, "David", "david@test.com", 28},
        {0, "Eve", std::nullopt, 22}
    };

    db->insert_many(users_table, batch);

    EXPECT_EQ(db->from(users_table).count(), 3);
}

TEST_F(MssqlIntegrationTest, QueryWhereFilter) {
    db->insert(users_table, User{0, "Alice", "alice@test.com", 30});
    db->insert(users_table, User{0, "Bob", "bob@test.com", 20});
    db->insert(users_table, User{0, "Charlie", "charlie@test.com", 40});

    auto adults = db->from(users_table)
                    .where(users_table["age"] >= 25)
                    .order_by(users_table["age"])
                    .to_vector();

    ASSERT_EQ(adults.size(), 2);
    EXPECT_EQ(adults[0].name, "Alice");
    EXPECT_EQ(adults[0].age, 30);
    EXPECT_EQ(adults[1].name, "Charlie");
    EXPECT_EQ(adults[1].age, 40);
}

TEST_F(MssqlIntegrationTest, QueryOrderByAndLimitOffset) {
    db->insert(users_table, User{0, "User1", std::nullopt, 10});
    db->insert(users_table, User{0, "User2", std::nullopt, 30});
    db->insert(users_table, User{0, "User3", std::nullopt, 20});
    db->insert(users_table, User{0, "User4", std::nullopt, 50});
    db->insert(users_table, User{0, "User5", std::nullopt, 40});

    auto page = db->from(users_table)
                  .order_by_desc(users_table["age"])
                  .limit(2)
                  .offset(1)
                  .to_vector();

    ASSERT_EQ(page.size(), 2);
    EXPECT_EQ(page[0].age, 40); // second highest
    EXPECT_EQ(page[1].age, 30); // third highest
}

TEST_F(MssqlIntegrationTest, QueryFirst) {
    db->insert(users_table, User{0, "Alice", "alice@test.com", 30});
    db->insert(users_table, User{0, "Bob", "bob@test.com", 25});

    auto found = db->from(users_table)
                   .where(users_table["name"] == "Alice")
                   .first();

    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->name, "Alice");
    EXPECT_EQ(found->age, 30);

    auto not_found = db->from(users_table)
                       .where(users_table["name"] == "Zach")
                       .first();

    EXPECT_FALSE(not_found.has_value());
}

TEST_F(MssqlIntegrationTest, Aggregates) {
    db->insert(users_table, User{0, "U1", std::nullopt, 10});
    db->insert(users_table, User{0, "U2", std::nullopt, 20});
    db->insert(users_table, User{0, "U3", std::nullopt, 30});
    db->insert(users_table, User{0, "U4", std::nullopt, 40});

    EXPECT_EQ(db->from(users_table).count(), 4);
    EXPECT_EQ(db->from(users_table).where(users_table["age"] > 20).count(), 2);

    auto avg_val = db->from(users_table).avg(users_table["age"]);
    ASSERT_TRUE(avg_val.has_value());
    EXPECT_DOUBLE_EQ(*avg_val, 25.0);

    auto sum_val = db->from(users_table).sum(users_table["age"]);
    ASSERT_TRUE(sum_val.has_value());
    EXPECT_DOUBLE_EQ(*sum_val, 100.0);

    auto min_val = db->from(users_table).min_val(users_table["age"]);
    ASSERT_TRUE(min_val.has_value());
    EXPECT_DOUBLE_EQ(*min_val, 10.0);

    auto max_val = db->from(users_table).max_val(users_table["age"]);
    ASSERT_TRUE(max_val.has_value());
    EXPECT_DOUBLE_EQ(*max_val, 40.0);
}

TEST_F(MssqlIntegrationTest, UpdateRecords) {
    int64_t id = db->insert(users_table, User{0, "Alice", "alice@old.com", 30});

    size_t affected = db->from(users_table)
                        .where(users_table["id"] == id)
                        .update({
                            users_table["email"] = "alice@new.com",
                            users_table["age"] = 31
                        });

    EXPECT_EQ(affected, 1);

    auto updated = db->from(users_table)
                     .where(users_table["id"] == id)
                     .first();

    ASSERT_TRUE(updated.has_value());
    EXPECT_EQ(updated->name, "Alice");
    ASSERT_TRUE(updated->email.has_value());
    EXPECT_EQ(*updated->email, "alice@new.com");
    EXPECT_EQ(updated->age, 31);
}

TEST_F(MssqlIntegrationTest, DeleteRecords) {
    db->insert(users_table, User{0, "ToKeep", std::nullopt, 25});
    int64_t del_id = db->insert(users_table, User{0, "ToDelete", std::nullopt, 50});

    EXPECT_EQ(db->from(users_table).count(), 2);

    size_t deleted = db->from(users_table)
                       .where(users_table["id"] == del_id)
                       .remove();

    EXPECT_EQ(deleted, 1);
    EXPECT_EQ(db->from(users_table).count(), 1);

    auto remaining = db->from(users_table).first();
    ASSERT_TRUE(remaining.has_value());
    EXPECT_EQ(remaining->name, "ToKeep");
}

TEST_F(MssqlIntegrationTest, TransactionCommit) {
    {
        auto txn = db->begin_transaction();
        db->insert(users_table, User{0, "CommittedUser", "comm@test.com", 45});
        txn.commit();
    }

    auto user = db->from(users_table)
                  .where(users_table["name"] == "CommittedUser")
                  .first();

    ASSERT_TRUE(user.has_value());
    EXPECT_EQ(user->name, "CommittedUser");
}

TEST_F(MssqlIntegrationTest, TransactionRollback) {
    {
        auto txn = db->begin_transaction();
        db->insert(users_table, User{0, "RollbackUser", "rb@test.com", 99});
    }

    auto user = db->from(users_table)
                  .where(users_table["name"] == "RollbackUser")
                  .first();

    EXPECT_FALSE(user.has_value());
    EXPECT_EQ(db->from(users_table).count(), 0);
}

TEST_F(MssqlIntegrationTest, NullAndNotNullFilters) {
    db->insert(users_table, User{0, "HasEmail1", "user1@example.com", 20});
    db->insert(users_table, User{0, "NoEmail1", std::nullopt, 21});
    db->insert(users_table, User{0, "HasEmail2", "user2@example.com", 22});
    db->insert(users_table, User{0, "NoEmail2", std::nullopt, 23});

    auto null_list = db->from(users_table)
                       .where(users_table["email"].is_null())
                       .to_vector();

    EXPECT_EQ(null_list.size(), 2);
    for (const auto& u : null_list) {
        EXPECT_FALSE(u.email.has_value());
    }

    auto not_null_list = db->from(users_table)
                           .where(users_table["email"].is_not_null())
                           .to_vector();

    EXPECT_EQ(not_null_list.size(), 2);
    for (const auto& u : not_null_list) {
        EXPECT_TRUE(u.email.has_value());
    }
}

TEST_F(MssqlIntegrationTest, BetweenAndLikeFilters) {
    db->insert(users_table, User{0, "Alice", "alice@example.com", 20});
    db->insert(users_table, User{0, "Bob", "bob@example.com", 30});
    db->insert(users_table, User{0, "Charlie", "charlie@example.com", 40});
    db->insert(users_table, User{0, "David", "david@example.com", 50});

    auto between_list = db->from(users_table)
                          .where(users_table["age"].between(25, 45))
                          .order_by(users_table["age"])
                          .to_vector();

    ASSERT_EQ(between_list.size(), 2);
    EXPECT_EQ(between_list[0].name, "Bob");
    EXPECT_EQ(between_list[1].name, "Charlie");

    auto like_list = db->from(users_table)
                       .where(users_table["name"].like("A%"))
                       .to_vector();

    ASSERT_EQ(like_list.size(), 1);
    EXPECT_EQ(like_list[0].name, "Alice");
}

TEST_F(MssqlIntegrationTest, InListAndMultiColumnThenBy) {
    db->insert(users_table, User{0, "Alice", "a@test.com", 30});
    db->insert(users_table, User{0, "Bob", "b@test.com", 30});
    db->insert(users_table, User{0, "Charlie", "c@test.com", 20});

    auto in_list = db->from(users_table)
                     .where(users_table["age"].in_list({20, 30}))
                     .order_by(users_table["age"], SortDir::Asc)
                     .then_by(users_table["name"], SortDir::Desc)
                     .to_vector();

    ASSERT_EQ(in_list.size(), 3);
    EXPECT_EQ(in_list[0].name, "Charlie");
    EXPECT_EQ(in_list[1].name, "Bob");
    EXPECT_EQ(in_list[2].name, "Alice");
}

TEST_F(MssqlIntegrationTest, SqlFunctionsFilter) {
    db->insert(users_table, User{0, "ALICE", "alice@example.com", 30});
    db->insert(users_table, User{0, "Bob", "bob@example.com", 25});

    auto res = db->from(users_table)
                 .where(users_table["name"].lower() == "alice")
                 .to_vector();

    ASSERT_EQ(res.size(), 1);
    EXPECT_EQ(res[0].name, "ALICE");

    auto len_res = db->from(users_table)
                     .where(length(users_table["name"]) == 3)
                     .to_vector();

    ASSERT_EQ(len_res.size(), 1);
    EXPECT_EQ(len_res[0].name, "Bob");
}

TEST_F(MssqlIntegrationTest, GroupByAndCountDistinct) {
    db->insert(users_table, User{0, "Alice", "a1@test.com", 30});
    db->insert(users_table, User{0, "Alice", "a2@test.com", 30});
    db->insert(users_table, User{0, "Bob", "b1@test.com", 25});

    size_t distinct_names = db->from(users_table).count_distinct(users_table["name"]);
    EXPECT_EQ(distinct_names, 2);

    auto grouped = db->from(users_table)
                     .group_by(users_table["id"], users_table["name"], users_table["email"], users_table["age"])
                     .having(users_table["age"] >= 30)
                     .to_vector();

    ASSERT_EQ(grouped.size(), 2);
    EXPECT_EQ(grouped[0].age, 30);
    EXPECT_EQ(grouped[1].age, 30);
}

TEST_F(MssqlIntegrationTest, InnerJoinAndLeftJoin) {
    int64_t u1_id = db->insert(users_table, User{0, "Alice", "alice@example.com", 30});
    int64_t u2_id = db->insert(users_table, User{0, "Bob", "bob@example.com", 25});

    db->insert(orders_table, Order{0, static_cast<int>(u1_id), 99.5});
    db->insert(orders_table, Order{0, static_cast<int>(u1_id), 150.0});

    // Inner Join: Alice has 2 orders, Bob has 0
    auto inner_rows = db->from(users_table)
                        .join(orders_table).on(users_table["id"] == orders_table["user_id"])
                        .order_by(orders_table["amount"])
                        .to_vector();

    ASSERT_EQ(inner_rows.size(), 2);
    EXPECT_EQ(inner_rows[0].first.name, "Alice");
    EXPECT_DOUBLE_EQ(inner_rows[0].second.amount, 99.5);
    EXPECT_EQ(inner_rows[1].first.name, "Alice");
    EXPECT_DOUBLE_EQ(inner_rows[1].second.amount, 150.0);

    // Left Join: Alice has 2 orders, Bob has 0 (nullopt)
    auto left_rows = db->from(users_table)
                       .left_join(orders_table).on(users_table["id"] == orders_table["user_id"])
                       .order_by(users_table["id"])
                       .then_by(orders_table["amount"])
                       .to_vector();

    ASSERT_EQ(left_rows.size(), 3);
    EXPECT_EQ(left_rows[0].first.name, "Alice");
    ASSERT_TRUE(left_rows[0].second.has_value());
    EXPECT_DOUBLE_EQ(left_rows[0].second->amount, 99.5);

    EXPECT_EQ(left_rows[1].first.name, "Alice");
    ASSERT_TRUE(left_rows[1].second.has_value());
    EXPECT_DOUBLE_EQ(left_rows[1].second->amount, 150.0);

    EXPECT_EQ(left_rows[2].first.name, "Bob");
    EXPECT_FALSE(left_rows[2].second.has_value());
}
