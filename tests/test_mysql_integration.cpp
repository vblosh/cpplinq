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

} // namespace

// ============================================================================
// MySQL Integration Tests
// ============================================================================

class MysqlIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        const char* env_conn = std::getenv("CPPLINQ_MYSQL_ODBC");
        if (!env_conn || env_conn[0] == '\0') {
            env_conn = std::getenv("CPPDB_MYSQL_ODBC");
        }
        if (!env_conn || env_conn[0] == '\0') {
            GTEST_SKIP() << "CPPLINQ_MYSQL_ODBC (or CPPDB_MYSQL_ODBC) environment variable is not defined. Skipping MySQL integration tests.";
            return;
        }

        conn_str_ = env_conn;
        try {
            db = std::make_unique<DbContext<mysql>>(conn_str_);
            
            // Clean up tables
            try {
                db->execute_raw("DROP TABLE IF EXISTS `test_employees`");
                db->execute_raw("DROP TABLE IF EXISTS `test_events`");
                db->execute_raw("DROP TABLE IF EXISTS `test_accounts`");
                db->execute_raw("DROP TABLE IF EXISTS `test_orders`");
                db->execute_raw("DROP TABLE IF EXISTS `test_users`");
            } catch (...) {}
            
            db->ensure_table(users_table);
            db->ensure_table(orders_table);
            db->ensure_table(accounts_table);
            db->ensure_table(events_table);
            db->ensure_table(employees_table);
        } catch (const std::exception& e) {
            GTEST_SKIP() << "Failed to connect to MySQL using CPPLINQ_MYSQL_ODBC (" << conn_str_ << "): " << e.what();
        }
    }

    void TearDown() override {
        if (db) {
            try {
                db->execute_raw("DROP TABLE IF EXISTS `test_employees`");
                db->execute_raw("DROP TABLE IF EXISTS `test_events`");
                db->execute_raw("DROP TABLE IF EXISTS `test_accounts`");
                db->execute_raw("DROP TABLE IF EXISTS `test_orders`");
                db->execute_raw("DROP TABLE IF EXISTS `test_users`");
            } catch (...) {}
        }
    }

    std::string conn_str_;
    std::unique_ptr<DbContext<mysql>> db;
};

TEST_F(MysqlIntegrationTest, InsertAndSelectAutoIncrement) {
    auto u1_id = db->insert(users_table, User{0, "Alice", "alice@example.com", 30});
    auto u2_id = db->insert(users_table, User{0, "Bob", std::nullopt, 25});

    EXPECT_GT(u1_id, 0);
    EXPECT_GT(u2_id, u1_id);

    auto users = db->from(users_table)
                   .order_by(users_table["id"])
                   .to_vector();

    ASSERT_EQ(users.size(), 2);
    EXPECT_EQ(users[0].name, "Alice");
    EXPECT_EQ(users[0].email, "alice@example.com");
    EXPECT_EQ(users[0].age, 30);

    EXPECT_EQ(users[1].name, "Bob");
    EXPECT_FALSE(users[1].email.has_value());
    EXPECT_EQ(users[1].age, 25);
}

TEST_F(MysqlIntegrationTest, FilteringAndSorting) {
    db->insert(users_table, User{0, "Alice", "alice@example.com", 30});
    db->insert(users_table, User{0, "Bob", "bob@example.com", 25});
    db->insert(users_table, User{0, "Charlie", "charlie@example.com", 35});

    auto filtered = db->from(users_table)
                      .where(users_table["age"] >= 30)
                      .order_by_desc(users_table["age"])
                      .to_vector();

    ASSERT_EQ(filtered.size(), 2);
    EXPECT_EQ(filtered[0].name, "Charlie");
    EXPECT_EQ(filtered[1].name, "Alice");
}

TEST_F(MysqlIntegrationTest, Pagination) {
    for (int i = 1; i <= 10; ++i) {
        db->insert(users_table, User{0, "User" + std::to_string(i), std::nullopt, 20 + i});
    }

    auto page = db->from(users_table)
                  .order_by(users_table["id"])
                  .limit(3)
                  .offset(3)
                  .to_vector();

    ASSERT_EQ(page.size(), 3);
    EXPECT_EQ(page[0].name, "User4");
    EXPECT_EQ(page[1].name, "User5");
    EXPECT_EQ(page[2].name, "User6");
}

TEST_F(MysqlIntegrationTest, UpdateAndRemove) {
    auto id = db->insert(users_table, User{0, "Alice", "alice@test.com", 30});

    size_t updated = db->from(users_table)
                       .where(users_table["id"] == id)
                       .update({
                           users_table["email"] = "alice.new@test.com",
                           users_table["age"] = 31
                       });
    EXPECT_EQ(updated, 1);

    auto u = db->from(users_table).where(users_table["id"] == id).first();
    ASSERT_TRUE(u.has_value());
    EXPECT_EQ(u->email, "alice.new@test.com");
    EXPECT_EQ(u->age, 31);

    size_t removed = db->from(users_table)
                       .where(users_table["id"] == id)
                       .remove();
    EXPECT_EQ(removed, 1);
    EXPECT_EQ(db->from(users_table).count(), 0);
}

TEST_F(MysqlIntegrationTest, Aggregates) {
    db->insert(users_table, User{0, "Alice", "alice@test.com", 20});
    db->insert(users_table, User{0, "Bob", "bob@test.com", 30});
    db->insert(users_table, User{0, "Charlie", "charlie@test.com", 40});

    EXPECT_EQ(db->from(users_table).count(), 3);
    EXPECT_DOUBLE_EQ(db->from(users_table).sum(users_table["age"]).value_or(0.0), 90.0);
    EXPECT_DOUBLE_EQ(db->from(users_table).avg(users_table["age"]).value_or(0.0), 30.0);
    EXPECT_DOUBLE_EQ(db->from(users_table).min_val(users_table["age"]).value_or(0.0), 20.0);
    EXPECT_DOUBLE_EQ(db->from(users_table).max_val(users_table["age"]).value_or(0.0), 40.0);
}

TEST_F(MysqlIntegrationTest, Transactions) {
    {
        auto txn = db->begin_transaction();
        db->insert(users_table, User{0, "RollbackUser", std::nullopt, 20});
        // No commit -> rollback
    }
    EXPECT_EQ(db->from(users_table).count(), 0);

    {
        auto txn = db->begin_transaction();
        db->insert(users_table, User{0, "CommittedUser", std::nullopt, 20});
        txn.commit();
    }
    EXPECT_EQ(db->from(users_table).count(), 1);
}

TEST_F(MysqlIntegrationTest, Joins) {
    auto u1_id = db->insert(users_table, User{0, "Alice", "alice@test.com", 30});
    auto u2_id = db->insert(users_table, User{0, "Bob", "bob@test.com", 25});

    db->insert(orders_table, Order{0, static_cast<int>(u1_id), 150.0});
    db->insert(accounts_table, Account{"Alice", "alice@test.com", 500});

    // 2-table INNER JOIN
    auto user_orders = db->from(users_table)
                         .join(orders_table).on(users_table["id"] == orders_table["user_id"])
                         .to_vector();

    ASSERT_EQ(user_orders.size(), 1);
    EXPECT_EQ(user_orders[0].first.name, "Alice");
    EXPECT_DOUBLE_EQ(user_orders[0].second.amount, 150.0);

    // 3-table JOIN
    auto multi_res = db->from(users_table)
                       .join(orders_table).on(users_table["id"] == orders_table["user_id"])
                       .join(accounts_table).on(users_table["name"] == accounts_table["username"])
                       .to_vector();

    ASSERT_EQ(multi_res.size(), 1);
    const auto& [u, o, a] = multi_res[0];
    EXPECT_EQ(u.name, "Alice");
    EXPECT_DOUBLE_EQ(o.amount, 150.0);
    EXPECT_EQ(a.points, 500);
}

TEST_F(MysqlIntegrationTest, Subqueries) {
    auto u1_id = db->insert(users_table, User{0, "Alice", "alice@test.com", 30});
    auto u2_id = db->insert(users_table, User{0, "Bob", "bob@test.com", 25});

    db->insert(orders_table, Order{0, static_cast<int>(u1_id), 150.0});

    auto buyers = db->from(users_table)
                    .where(exists(
                        db->from(orders_table)
                          .where(orders_table["user_id"] == users_table["id"])
                    ))
                    .to_vector();

    ASSERT_EQ(buyers.size(), 1);
    EXPECT_EQ(buyers[0].name, "Alice");
}

TEST_F(MysqlIntegrationTest, Upsert) {
    User user{1, "Alice", "alice@test.com", 30};
    db->upsert(users_table, user, {"id"}, {"name", "email", "age"});

    EXPECT_EQ(db->from(users_table).count(), 1);

    User updated{1, "Alice Updated", "alice.new@test.com", 31};
    db->upsert(users_table, updated, {"id"}, {"name", "email", "age"});

    EXPECT_EQ(db->from(users_table).count(), 1);
    auto u = db->from(users_table).where(users_table["id"] == 1).first();
    ASSERT_TRUE(u.has_value());
    EXPECT_EQ(u->name, "Alice Updated");
    EXPECT_EQ(u->age, 31);
}
