#include <gtest/gtest.h>
#include "cpplinq/cpplinq.hpp"
#include <string>
#include <optional>
#include <vector>
#include <tuple>

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

// Define table schema for PostgreSQL
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

struct Account {
    std::string username;
    std::string email;
    int points = 0;

    bool operator==(const Account& other) const = default;
};

inline const auto accounts_table = table<Account>(
    "test_accounts",
    column("username", &Account::username, primary_key),
    column("email", &Account::email),
    column("points", &Account::points)
);

struct Event {
    int id = 0;
    std::string name;
    std::string event_date;

    bool operator==(const Event& other) const = default;
};

inline const auto events_table = table<Event>(
    "test_events",
    column("id", &Event::id, primary_key, auto_increment),
    column("name", &Event::name),
    column("event_date", &Event::event_date)
);

struct Employee {
    int id = 0;
    std::string name;
    std::string department;
    int salary = 0;

    bool operator==(const Employee& other) const = default;
};

inline const auto employees_table = table<Employee>(
    "test_employees",
    column("id", &Employee::id, primary_key, auto_increment),
    column("name", &Employee::name),
    column("department", &Employee::department),
    column("salary", &Employee::salary)
);

struct Measurement {
    int id = 0;
    uint64_t large_counter = 0;
    SqlNumeric high_precision_val;
    SqlDate recorded_date;
    SqlTime recorded_time;
    SqlTimestamp recorded_at;
    SqlInterval duration;

    bool operator==(const Measurement& other) const = default;
};

inline const auto measurements_table = table<Measurement>(
    "test_measurements",
    column("id", &Measurement::id, primary_key, auto_increment),
    column("large_counter", &Measurement::large_counter),
    column("high_precision_val", &Measurement::high_precision_val),
    column("recorded_date", &Measurement::recorded_date),
    column("recorded_time", &Measurement::recorded_time),
    column("recorded_at", &Measurement::recorded_at),
    column("duration", &Measurement::duration)
);

} // namespace

// ============================================================================
// PostgreSQL Integration Tests using PostgreSQL35W DSN
// ============================================================================

class PostgresIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        const char* env_conn = std::getenv("CPPLINQ_POSTGRES_ODBC");
        if (!env_conn || env_conn[0] == '\0') {
            env_conn = std::getenv("CPPDB_POSTGRES_ODBC");
        }
        if (!env_conn || env_conn[0] == '\0') {
            GTEST_SKIP() << "CPPLINQ_POSTGRES_ODBC (or CPPDB_POSTGRES_ODBC) environment variable is not defined. Skipping PostgreSQL integration tests.";
            return;
        }

        conn_str_ = env_conn;
        try {
            // Connect to PostgreSQL using CPPLINQ_POSTGRES_ODBC DSN / connection string
            db = std::make_unique<DbContext<postgres>>(conn_str_);
            
            // Clean up table if exists from prior test runs
            try {
                db->execute_raw("DROP TABLE IF EXISTS \"test_measurements\"");
                db->execute_raw("DROP TABLE IF EXISTS \"test_employees\"");
                db->execute_raw("DROP TABLE IF EXISTS \"test_events\"");
                db->execute_raw("DROP TABLE IF EXISTS \"test_accounts\"");
                db->execute_raw("DROP TABLE IF EXISTS \"test_orders\"");
                db->execute_raw("DROP TABLE IF EXISTS \"test_users\"");
            } catch (...) {}
            
            db->ensure_table(users_table);
            db->ensure_table(orders_table);
            db->ensure_table(accounts_table);
            db->ensure_table(events_table);
            db->ensure_table(employees_table);
            db->ensure_table(measurements_table);
        } catch (const std::exception& e) {
            GTEST_SKIP() << "Failed to connect to PostgreSQL using CPPLINQ_POSTGRES_ODBC (" << conn_str_ << "): " << e.what();
        }
    }

    void TearDown() override {
        if (db) {
            try {
                db->execute_raw("DROP TABLE IF EXISTS \"test_measurements\"");
                db->execute_raw("DROP TABLE IF EXISTS \"test_employees\"");
                db->execute_raw("DROP TABLE IF EXISTS \"test_events\"");
                db->execute_raw("DROP TABLE IF EXISTS \"test_accounts\"");
                db->execute_raw("DROP TABLE IF EXISTS \"test_orders\"");
                db->execute_raw("DROP TABLE IF EXISTS \"test_users\"");
            } catch (...) {}
        }
    }

    std::string conn_str_;
    std::unique_ptr<DbContext<postgres>> db;
};

TEST_F(PostgresIntegrationTest, EnsureTableAndInsertSingle) {
    User u1{0, "Alice", "alice@example.com", 30};
    int64_t id1 = db->insert(users_table, u1);
    EXPECT_GT(id1, 0);

    User u2{0, "Bob", std::nullopt, 25};
    int64_t id2 = db->insert(users_table, u2);
    EXPECT_GT(id2, id1);

    auto all = db->from(users_table)
                 .order_by(users_table["id"])
                 .to_list();
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

TEST_F(PostgresIntegrationTest, ConnectWithDsnPrefix) {
    std::string dsn_prefix_str = (conn_str_.rfind("DSN=", 0) == 0 || conn_str_.rfind("dsn=", 0) == 0)
                                 ? conn_str_
                                 : "DSN=" + conn_str_;
    auto dsn_db = cpplinq::connect<postgres>(dsn_prefix_str);
    EXPECT_TRUE(dsn_db.connection().is_open());
    EXPECT_EQ(dsn_db.from(users_table).count(), 0);
}

TEST_F(PostgresIntegrationTest, InsertMany) {
    std::vector<User> batch = {
        {0, "Charlie", "charlie@test.com", 35},
        {0, "David", "david@test.com", 28},
        {0, "Eve", std::nullopt, 22}
    };

    db->insert_many(users_table, batch);

    EXPECT_EQ(db->from(users_table).count(), 3);
}

TEST_F(PostgresIntegrationTest, QueryWhereFilter) {
    db->insert(users_table, User{0, "Alice", "alice@test.com", 30});
    db->insert(users_table, User{0, "Bob", "bob@test.com", 20});
    db->insert(users_table, User{0, "Charlie", "charlie@test.com", 40});

    auto adults = db->from(users_table)
                    .where(users_table["age"] >= 25)
                    .order_by(users_table["age"])
                    .to_list();

    ASSERT_EQ(adults.size(), 2);
    EXPECT_EQ(adults[0].name, "Alice");
    EXPECT_EQ(adults[0].age, 30);
    EXPECT_EQ(adults[1].name, "Charlie");
    EXPECT_EQ(adults[1].age, 40);
}

TEST_F(PostgresIntegrationTest, QueryOrderByAndLimitOffset) {
    db->insert(users_table, User{0, "User1", std::nullopt, 10});
    db->insert(users_table, User{0, "User2", std::nullopt, 30});
    db->insert(users_table, User{0, "User3", std::nullopt, 20});
    db->insert(users_table, User{0, "User4", std::nullopt, 50});
    db->insert(users_table, User{0, "User5", std::nullopt, 40});

    // Descending order with limit and offset
    auto page = db->from(users_table)
                  .order_by_desc(users_table["age"])
                  .limit(2)
                  .offset(1)
                  .to_list();

    ASSERT_EQ(page.size(), 2);
    EXPECT_EQ(page[0].age, 40); // second highest
    EXPECT_EQ(page[1].age, 30); // third highest
}

TEST_F(PostgresIntegrationTest, QueryFirst) {
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

TEST_F(PostgresIntegrationTest, Aggregates) {
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

TEST_F(PostgresIntegrationTest, UpdateRecords) {
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

TEST_F(PostgresIntegrationTest, DeleteRecords) {
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

TEST_F(PostgresIntegrationTest, TransactionCommit) {
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

TEST_F(PostgresIntegrationTest, TransactionRollback) {
    {
        auto txn = db->begin_transaction();
        db->insert(users_table, User{0, "RollbackUser", "rb@test.com", 99});
        // Destructor executes rollback because commit() was not called
    }

    auto user = db->from(users_table)
                  .where(users_table["name"] == "RollbackUser")
                  .first();

    EXPECT_FALSE(user.has_value());
    EXPECT_EQ(db->from(users_table).count(), 0);
}

TEST_F(PostgresIntegrationTest, NullAndNotNullFilters) {
    db->insert(users_table, User{0, "HasEmail1", "user1@example.com", 20});
    db->insert(users_table, User{0, "NoEmail1", std::nullopt, 21});
    db->insert(users_table, User{0, "HasEmail2", "user2@example.com", 22});
    db->insert(users_table, User{0, "NoEmail2", std::nullopt, 23});

    auto null_list = db->from(users_table)
                       .where(users_table["email"].is_null())
                       .to_list();

    EXPECT_EQ(null_list.size(), 2);
    for (const auto& u : null_list) {
        EXPECT_FALSE(u.email.has_value());
    }

    auto not_null_list = db->from(users_table)
                           .where(users_table["email"].is_not_null())
                           .to_list();

    EXPECT_EQ(not_null_list.size(), 2);
    for (const auto& u : not_null_list) {
        EXPECT_TRUE(u.email.has_value());
    }
}

TEST_F(PostgresIntegrationTest, BetweenAndLikeFilters) {
    db->insert(users_table, User{0, "Alice", "alice@example.com", 20});
    db->insert(users_table, User{0, "Bob", "bob@example.com", 30});
    db->insert(users_table, User{0, "Charlie", "charlie@example.com", 40});
    db->insert(users_table, User{0, "David", "david@example.com", 50});

    auto between_list = db->from(users_table)
                          .where(users_table["age"].between(25, 45))
                          .order_by(users_table["age"])
                          .to_list();

    ASSERT_EQ(between_list.size(), 2);
    EXPECT_EQ(between_list[0].name, "Bob");
    EXPECT_EQ(between_list[1].name, "Charlie");

    auto like_list = db->from(users_table)
                       .where(users_table["name"].like("A%"))
                       .to_list();

    ASSERT_EQ(like_list.size(), 1);
    EXPECT_EQ(like_list[0].name, "Alice");
}

TEST_F(PostgresIntegrationTest, InListAndMultiColumnThenBy) {
    db->insert(users_table, User{0, "Alice", "a@test.com", 30});
    db->insert(users_table, User{0, "Bob", "b@test.com", 30});
    db->insert(users_table, User{0, "Charlie", "c@test.com", 20});

    auto in_list = db->from(users_table)
                     .where(users_table["age"].in_list({20, 30}))
                     .order_by(users_table["age"], SortDir::Asc)
                     .then_by(users_table["name"], SortDir::Desc)
                     .to_list();

    ASSERT_EQ(in_list.size(), 3);
    EXPECT_EQ(in_list[0].name, "Charlie");
    EXPECT_EQ(in_list[1].name, "Bob");
    EXPECT_EQ(in_list[2].name, "Alice");
}

TEST_F(PostgresIntegrationTest, SqlFunctionsFilter) {
    db->insert(users_table, User{0, "ALICE", "alice@example.com", 30});
    db->insert(users_table, User{0, "Bob", "bob@example.com", 25});

    auto res = db->from(users_table)
                 .where(users_table["name"].lower() == "alice")
                 .to_list();

    ASSERT_EQ(res.size(), 1);
    EXPECT_EQ(res[0].name, "ALICE");

    auto len_res = db->from(users_table)
                     .where(length(users_table["name"]) == 3)
                     .to_list();

    ASSERT_EQ(len_res.size(), 1);
    EXPECT_EQ(len_res[0].name, "Bob");
}

TEST_F(PostgresIntegrationTest, GroupByAndCountDistinct) {
    db->insert(users_table, User{0, "Alice", "a1@test.com", 30});
    db->insert(users_table, User{0, "Alice", "a2@test.com", 30});
    db->insert(users_table, User{0, "Bob", "b1@test.com", 25});

    size_t distinct_names = db->from(users_table).count_distinct(users_table["name"]);
    EXPECT_EQ(distinct_names, 2);

    auto grouped = db->from(users_table)
                     .group_by(users_table["id"], users_table["name"], users_table["email"], users_table["age"])
                     .having(users_table["age"] >= 30)
                     .to_list();

    ASSERT_EQ(grouped.size(), 2);
    EXPECT_EQ(grouped[0].age, 30);
    EXPECT_EQ(grouped[1].age, 30);
}

TEST_F(PostgresIntegrationTest, InnerJoinAndLeftJoin) {
    int64_t u1_id = db->insert(users_table, User{0, "Alice", "alice@example.com", 30});
    int64_t u2_id = db->insert(users_table, User{0, "Bob", "bob@example.com", 25});

    db->insert(orders_table, Order{0, static_cast<int>(u1_id), 99.5});
    db->insert(orders_table, Order{0, static_cast<int>(u1_id), 150.0});

    // Inner Join: Alice has 2 orders, Bob has 0
    auto inner_rows = db->from(users_table)
                        .join(orders_table).on(users_table["id"] == orders_table["user_id"])
                        .order_by(orders_table["amount"])
                        .to_list();

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
                       .to_list();

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

TEST_F(PostgresIntegrationTest, UpsertEntity) {
    db->upsert(accounts_table, Account{"alice", "alice@initial.com", 100}, {"username"}, {"email", "points"});

    auto u = db->from(accounts_table).where(accounts_table["username"] == "alice").first();
    ASSERT_TRUE(u.has_value());
    EXPECT_EQ(u->username, "alice");
    EXPECT_EQ(u->email, "alice@initial.com");
    EXPECT_EQ(u->points, 100);

    // Upsert with same username updates the record
    db->upsert(accounts_table, Account{"alice", "alice@updated.com", 250}, {"username"}, {"email", "points"});

    auto u_upd = db->from(accounts_table).where(accounts_table["username"] == "alice").first();
    ASSERT_TRUE(u_upd.has_value());
    EXPECT_EQ(u_upd->username, "alice");
    EXPECT_EQ(u_upd->email, "alice@updated.com");
    EXPECT_EQ(u_upd->points, 250);
}

TEST_F(PostgresIntegrationTest, SetOperations) {
    db->insert(users_table, User{0, "Alice", "a@test.com", 20});
    db->insert(users_table, User{0, "Bob", "b@test.com", 30});
    db->insert(users_table, User{0, "Charlie", "c@test.com", 40});

    auto q1 = db->from(users_table).where(users_table["age"] <= 20);
    auto q2 = db->from(users_table).where(users_table["age"] >= 30);

    auto union_rows = q1.union_with(q2).order_by(users_table["age"]).to_list();
    ASSERT_EQ(union_rows.size(), 3);
    EXPECT_EQ(union_rows[0].name, "Alice");
    EXPECT_EQ(union_rows[1].name, "Bob");
    EXPECT_EQ(union_rows[2].name, "Charlie");

    auto union_all_rows = q1.union_all(q1).to_list();
    ASSERT_EQ(union_all_rows.size(), 2);
    EXPECT_EQ(union_all_rows[0].name, "Alice");
    EXPECT_EQ(union_all_rows[1].name, "Alice");

    auto intersect_rows = q1.intersect(db->from(users_table).where(users_table["age"] < 30)).to_list();
    ASSERT_EQ(intersect_rows.size(), 1);
    EXPECT_EQ(intersect_rows[0].name, "Alice");

    auto except_rows = db->from(users_table).except_from(q1).order_by(users_table["age"]).to_list();
    ASSERT_EQ(except_rows.size(), 2);
    EXPECT_EQ(except_rows[0].name, "Bob");
    EXPECT_EQ(except_rows[1].name, "Charlie");
}

TEST_F(PostgresIntegrationTest, ConnectionPoolWithPostgres) {
    PoolConfig config;
    config.min_connections = 2;
    config.max_connections = 4;

    auto pool = make_pool<postgres>(conn_str_, config);
    EXPECT_EQ(pool->size(), 2);

    pool->with_context([&](auto& db_ctx) {
        db_ctx.insert(users_table, User{0, "PoolUser", "pool@test.com", 28});
        auto users = db_ctx.from(users_table).where(users_table["name"] == "PoolUser").to_list();
        EXPECT_EQ(users.size(), 1);
        EXPECT_EQ(users[0].name, "PoolUser");
    });
}

TEST_F(PostgresIntegrationTest, SubqueriesExistsAndIn) {
    int64_t u1 = db->insert(users_table, User{0, "Alice", "a@test.com", 25});
    int64_t u2 = db->insert(users_table, User{0, "Bob", "b@test.com", 35});

    db->insert(orders_table, Order{0, static_cast<int>(u1), 150.0});

    // EXISTS: Alice has orders, Bob does not
    auto has_orders = db->from(users_table)
                        .where(exists(db->from(orders_table).where(orders_table["user_id"] == users_table["id"])))
                        .to_list();
    ASSERT_EQ(has_orders.size(), 1);
    EXPECT_EQ(has_orders[0].name, "Alice");

    // NOT EXISTS: Bob has no orders
    auto no_orders = db->from(users_table)
                       .where(not_exists(db->from(orders_table).where(orders_table["user_id"] == users_table["id"])))
                       .to_list();
    ASSERT_EQ(no_orders.size(), 1);
    EXPECT_EQ(no_orders[0].name, "Bob");

    // IN (subquery): Alice's id is in orders
    auto in_orders = db->from(users_table)
                       .where(users_table["id"].in(db->from(orders_table).where(orders_table["amount"] > 100.0).as_subquery(orders_table["user_id"])))
                       .to_list();
    ASSERT_EQ(in_orders.size(), 1);
    EXPECT_EQ(in_orders[0].name, "Alice");
}

TEST_F(PostgresIntegrationTest, DateTimeFunctions) {
    db->insert(events_table, Event{0, "SummerParty", "2026-08-16 14:00:00"});
    db->insert(events_table, Event{0, "WinterParty", "2025-01-10 18:00:00"});

    auto aug_events = db->from(events_table)
                        .where(events_table["event_date"].year() == 2026 && events_table["event_date"].month() == 8)
                        .to_list();
    ASSERT_EQ(aug_events.size(), 1);
    EXPECT_EQ(aug_events[0].name, "SummerParty");

    auto day10_events = db->from(events_table)
                          .where(events_table["event_date"].day() == 10)
                          .to_list();
    ASSERT_EQ(day10_events.size(), 1);
    EXPECT_EQ(day10_events[0].name, "WinterParty");
}

TEST_F(PostgresIntegrationTest, WindowFunctions) {
    db->insert(employees_table, Employee{0, "Alice", "Engineering", 120000});
    db->insert(employees_table, Employee{0, "Bob", "Engineering", 110000});
    db->insert(employees_table, Employee{0, "Charlie", "HR", 80000});
    db->insert(employees_table, Employee{0, "Dave", "HR", 90000});

    auto prep = db->connection().prepare("SELECT \"name\", \"department\", \"salary\", ROW_NUMBER() OVER (PARTITION BY \"department\" ORDER BY \"salary\" DESC) as rn FROM \"test_employees\" ORDER BY \"department\", rn;");
    auto reader = prep->execute_query();
    ASSERT_TRUE(reader->next());
    EXPECT_EQ(reader->get_string(0), "Alice");
    EXPECT_EQ(reader->get_int64(3), 1LL);
    ASSERT_TRUE(reader->next());
    EXPECT_EQ(reader->get_string(0), "Bob");
    EXPECT_EQ(reader->get_int64(3), 2LL);
    ASSERT_TRUE(reader->next());
    EXPECT_EQ(reader->get_string(0), "Dave");
    EXPECT_EQ(reader->get_int64(3), 1LL);
    ASSERT_TRUE(reader->next());
    EXPECT_EQ(reader->get_string(0), "Charlie");
    EXPECT_EQ(reader->get_int64(3), 2LL);
    EXPECT_FALSE(reader->next());
}

TEST_F(PostgresIntegrationTest, MultiTableJoins) {
    auto u1_id = db->insert(users_table, User{0, "Alice", "alice@test.com", 30});
    auto u2_id = db->insert(users_table, User{0, "Bob", "bob@test.com", 25});

    db->insert(orders_table, Order{0, static_cast<int>(u1_id), 150.0});
    db->insert(accounts_table, Account{"Alice", "alice@test.com", 500});
    db->insert(accounts_table, Account{"Bob", "bob@test.com", 300});

    // 3-Table Inner Join: User + Order + Account
    auto results = db->from(users_table)
                     .join(orders_table).on(users_table["id"] == orders_table["user_id"])
                     .join(accounts_table).on(users_table["name"] == accounts_table["username"])
                     .to_list();

    ASSERT_EQ(results.size(), 1);
    const auto& [user, order, account] = results[0];
    EXPECT_EQ(user.name, "Alice");
    EXPECT_DOUBLE_EQ(order.amount, 150.0);
    EXPECT_EQ(account.points, 500);

    // 3-Table Join with Left Join: User + Order (left) + Account (inner)
    auto left_results = db->from(users_table)
                          .left_join(orders_table).on(users_table["id"] == orders_table["user_id"])
                          .join(accounts_table).on(users_table["name"] == accounts_table["username"])
                          .order_by(users_table["name"])
                          .to_list();

    ASSERT_EQ(left_results.size(), 2);
    // Alice has order
    const auto& [u_alice, o_alice, a_alice] = left_results[0];
    EXPECT_EQ(u_alice.name, "Alice");
    EXPECT_TRUE(o_alice.has_value());
    EXPECT_EQ(a_alice.points, 500);
    // Bob has no order
    const auto& [u_bob, o_bob, a_bob] = left_results[1];
    EXPECT_EQ(u_bob.name, "Bob");
    EXPECT_FALSE(o_bob.has_value());
    EXPECT_EQ(a_bob.points, 300);
}

TEST_F(PostgresIntegrationTest, CommonTableExpressions) {
    db->insert(users_table, User{0, "Alice", "alice@test.com", 30});
    db->insert(users_table, User{0, "Bob", "bob@test.com", 25});
    db->insert(users_table, User{0, "Charlie", "charlie@test.com", 40});

    auto prep = db->connection().prepare(
        "WITH active_adults AS (SELECT \"id\", \"name\", \"age\" FROM \"test_users\" WHERE \"age\" >= 25) "
        "SELECT \"id\", \"name\", \"age\" FROM active_adults WHERE \"age\" >= 30 ORDER BY \"name\";"
    );
    auto reader = prep->execute_query();
    ASSERT_TRUE(reader->next());
    EXPECT_EQ(reader->get_string(1), "Alice");
    ASSERT_TRUE(reader->next());
    EXPECT_EQ(reader->get_string(1), "Charlie");
    EXPECT_FALSE(reader->next());
}

TEST_F(PostgresIntegrationTest, BulkOperationsAndChunking) {
    if (!db) GTEST_SKIP() << "PostgreSQL test instance not available";

    // 1. Batch insert with chunking
    std::vector<Account> accounts;
    for (int i = 0; i < 250; ++i) {
        accounts.push_back(Account{
            "user_" + std::to_string(i),
            "user_" + std::to_string(i) + "@example.com",
            i * 10
        });
    }
    db->insert_many(accounts_table, accounts, 50);

    auto count = db->from(accounts_table).count();
    EXPECT_EQ(count, 250);

    // 2. exists()
    EXPECT_TRUE(db->from(accounts_table).where(accounts_table["points"] == 100).exists());
    EXPECT_FALSE(db->from(accounts_table).where(accounts_table["points"] == 99999).exists());

    // 3. update_many
    for (auto& acc : accounts) {
        acc.points += 5;
    }
    size_t updated = db->update_many(accounts_table, accounts, 50);
    EXPECT_EQ(updated, 250);
    auto acc0 = db->from(accounts_table).where(accounts_table["username"] == "user_0").first();
    ASSERT_TRUE(acc0.has_value());
    EXPECT_EQ(acc0->points, 5);

    // 4. upsert_many
    std::vector<Account> upsert_batch;
    upsert_batch.push_back(Account{"user_0", "updated_user0@example.com", 999});
    upsert_batch.push_back(Account{"user_new_1", "new1@example.com", 100});
    db->upsert_many(accounts_table, upsert_batch);

    auto acc0_up = db->from(accounts_table).where(accounts_table["username"] == "user_0").first();
    ASSERT_TRUE(acc0_up.has_value());
    EXPECT_EQ(acc0_up->points, 999);
    EXPECT_EQ(acc0_up->email, "updated_user0@example.com");

    auto new_acc = db->from(accounts_table).where(accounts_table["username"] == "user_new_1").first();
    ASSERT_TRUE(new_acc.has_value());
    EXPECT_EQ(new_acc->points, 100);

    // 5. delete_many with chunking
    std::vector<std::string> del_ids;
    for (int i = 0; i < 100; ++i) {
        del_ids.push_back("user_" + std::to_string(i));
    }
    size_t deleted = db->delete_many(accounts_table, del_ids, 30);
    EXPECT_EQ(deleted, 100);
    EXPECT_EQ(db->from(accounts_table).count(), 151);

    // 6. truncate
    db->truncate(accounts_table);
    EXPECT_EQ(db->from(accounts_table).count(), 0);
}

TEST_F(PostgresIntegrationTest, DataTypingRoundTripAndQueries) {
    if (!db) return;

    Measurement m1;
    m1.large_counter = 18446744073709551600ULL;
    m1.high_precision_val = SqlNumeric("123456789.987654321");
    m1.recorded_date = SqlDate(2026, 8, 17);
    m1.recorded_time = SqlTime(19, 45, 30);
    m1.recorded_at = SqlTimestamp(2026, 8, 17, 19, 45, 30, 0);
    m1.duration = SqlInterval::from_day_second(2, 5, 30, 0);

    Measurement m2;
    m2.large_counter = 42ULL;
    m2.high_precision_val = SqlNumeric("-999.50");
    m2.recorded_date = SqlDate(2025, 12, 31);
    m2.recorded_time = SqlTime(23, 59, 59);
    m2.recorded_at = SqlTimestamp(2025, 12, 31, 23, 59, 59, 0);
    m2.duration = SqlInterval::from_day_second(0, 0, 0, 15);

    db->insert(measurements_table, m1);
    db->insert(measurements_table, m2);

    auto all_measurements = db->from(measurements_table).to_list().to_vector();
    ASSERT_EQ(all_measurements.size(), 2);

    EXPECT_EQ(all_measurements[0].large_counter, 18446744073709551600ULL);
    EXPECT_EQ(all_measurements[0].recorded_date.to_string(), "2026-08-17");
    EXPECT_EQ(all_measurements[0].recorded_time.to_string(), "19:45:30");
    EXPECT_EQ(all_measurements[0].recorded_at.to_string(), "2026-08-17 19:45:30");

    EXPECT_EQ(all_measurements[1].large_counter, 42ULL);
    EXPECT_EQ(all_measurements[1].recorded_date.to_string(), "2025-12-31");
    EXPECT_EQ(all_measurements[1].recorded_time.to_string(), "23:59:59");
    EXPECT_EQ(all_measurements[1].recorded_at.to_string(), "2025-12-31 23:59:59");

    // Filter queries with new types
    auto found_date = db->from(measurements_table)
        .where(measurements_table["recorded_date"] == SqlDate(2026, 8, 17))
        .first();
    ASSERT_TRUE(found_date.has_value());
    EXPECT_EQ(found_date->large_counter, 18446744073709551600ULL);

    auto found_time = db->from(measurements_table)
        .where(measurements_table["recorded_time"] == SqlTime(23, 59, 59))
        .first();
    ASSERT_TRUE(found_time.has_value());
    EXPECT_EQ(found_time->recorded_date.to_string(), "2025-12-31");

    auto found_ts = db->from(measurements_table)
        .where(measurements_table["recorded_at"] == SqlTimestamp(2026, 8, 17, 19, 45, 30, 0))
        .first();
    ASSERT_TRUE(found_ts.has_value());
    EXPECT_EQ(found_ts->large_counter, 18446744073709551600ULL);

    // Update with new types
    found_date->high_precision_val = SqlNumeric("555.777");
    found_date->duration = SqlInterval::from_day_second(1, 0, 0, 0);
    size_t updated = db->update_many(measurements_table, std::vector<Measurement>{*found_date});
    EXPECT_EQ(updated, 1);

    auto reloaded = db->from(measurements_table).where(measurements_table["id"] == found_date->id).first();
    ASSERT_TRUE(reloaded.has_value());
    EXPECT_EQ(reloaded->duration.to_string(), "1 00:00:00");
}
