#include <gtest/gtest.h>
#include "cpplinq/cpplinq.hpp"
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>

using namespace cpplinq;

namespace {

struct Item {
    int64_t id = 0;
    std::string name;
    int value = 0;
};

inline const auto items_table = table<Item>(
    "test_pool_items",
    column("id", &Item::id, primary_key, auto_increment),
    column("name", &Item::name),
    column("value", &Item::value)
);

} // namespace

TEST(ConnectionPoolTest, BasicAcquireAndReturn) {
    PoolConfig config;
    config.min_connections = 2;
    config.max_connections = 5;

    auto pool = make_pool<sqlite>(":memory:", config);
    EXPECT_EQ(pool->max_size(), 5);
    EXPECT_EQ(pool->size(), 2);
    EXPECT_EQ(pool->idle_count(), 2);
    EXPECT_EQ(pool->active_count(), 0);

    {
        auto conn1 = pool->acquire();
        EXPECT_TRUE(conn1.is_valid());
        EXPECT_EQ(pool->active_count(), 1);
        EXPECT_EQ(pool->idle_count(), 1);

        {
            auto conn2 = pool->acquire();
            EXPECT_TRUE(conn2.is_valid());
            EXPECT_EQ(pool->active_count(), 2);
            EXPECT_EQ(pool->idle_count(), 0);
        } // conn2 returned

        EXPECT_EQ(pool->active_count(), 1);
        EXPECT_EQ(pool->idle_count(), 1);
    } // conn1 returned

    EXPECT_EQ(pool->active_count(), 0);
    EXPECT_EQ(pool->idle_count(), 2);
}

TEST(ConnectionPoolTest, WithConnectionAndWithContext) {
    PoolConfig config;
    config.min_connections = 1;
    config.max_connections = 2;

    auto pool = make_pool<sqlite>(":memory:", config);

    pool->with_context([&](auto& db) {
        db.ensure_table(items_table);
        db.insert(items_table, Item{0, "Gadget", 42});
        auto count = db.from(items_table).count();
        EXPECT_EQ(count, 1);
    });

    pool->with_connection([&](IConnection& conn) {
        EXPECT_TRUE(conn.is_open());
    });
}

TEST(ConnectionPoolTest, TryAcquireTimeoutWhenExhausted) {
    PoolConfig config;
    config.min_connections = 1;
    config.max_connections = 1;
    config.acquire_timeout = std::chrono::milliseconds(50);

    auto pool = make_pool<sqlite>(":memory:", config);

    auto conn1 = pool->acquire();
    EXPECT_TRUE(conn1.is_valid());

    auto conn2 = pool->try_acquire(std::chrono::milliseconds(50));
    EXPECT_FALSE(conn2.has_value());
}

#include <filesystem>

TEST(ConnectionPoolTest, ConcurrentWorkerThreads) {
    PoolConfig config;
    config.min_connections = 2;
    config.max_connections = 4;
    config.acquire_timeout = std::chrono::milliseconds(10000);

    std::string db_path = (std::filesystem::temp_directory_path() / "test_pool_concurrent.db").string();
    std::filesystem::remove(db_path);

    auto pool = make_pool<sqlite>(db_path, config);

    // Initialize schema on one connection
    {
        auto conn = pool->acquire();
        auto db = conn.get_context();
        db.ensure_table(items_table);
    }

    constexpr int kNumThreads = 8;
    constexpr int kOpsPerThread = 20;
    std::atomic<int> completed_ops{0};

    std::vector<std::thread> workers;
    workers.reserve(kNumThreads);

    for (int t = 0; t < kNumThreads; ++t) {
        workers.emplace_back([&, t]() {
            for (int i = 0; i < kOpsPerThread; ++i) {
                auto conn = pool->acquire();
                auto db = conn.get_context();
                std::string item_name = "Thread_" + std::to_string(t) + "_Item_" + std::to_string(i);
                db.insert(items_table, Item{0, item_name, t * 100 + i});
                completed_ops.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    for (auto& w : workers) {
        w.join();
    }

    EXPECT_EQ(completed_ops.load(), kNumThreads * kOpsPerThread);
    EXPECT_EQ(pool->active_count(), 0);

    pool->close();
    std::filesystem::remove(db_path);
}

TEST(ConnectionPoolTest, PoolClose) {
    PoolConfig config;
    config.min_connections = 2;
    config.max_connections = 3;

    auto pool = make_pool<sqlite>(":memory:", config);
    EXPECT_FALSE(pool->is_closed());

    pool->close();
    EXPECT_TRUE(pool->is_closed());

    EXPECT_THROW(pool->acquire(), DbException);
}
