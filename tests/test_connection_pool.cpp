#include <gtest/gtest.h>
#include "cpplinq/cpplinq.hpp"
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <stop_token>

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

TEST(ConnectionPoolTest, ClosedConnectionIsDiscardedAndMinimumIsNotReplenished) {
    PoolConfig config;
    config.min_connections = 2;
    config.max_connections = 3;

    auto pool = make_pool<sqlite>(":memory:", config);
    ASSERT_EQ(pool->size(), 2);
    ASSERT_EQ(pool->idle_count(), 2);

    {
        auto conn = pool->acquire();
        ASSERT_TRUE(conn.is_valid());
        ASSERT_TRUE(conn->is_open());
        conn->close();
        EXPECT_FALSE(conn->is_open());
    }

    // The closed connection is discarded on RAII return and the pool does not
    // immediately restore min_connections.
    EXPECT_EQ(pool->size(), 1);
    EXPECT_EQ(pool->idle_count(), 1);
    EXPECT_EQ(pool->active_count(), 0);
}

TEST(ConnectionPoolTest, AcquireAfterDiscardOpensHealthyConnection) {
    PoolConfig config;
    config.min_connections = 1;
    config.max_connections = 1;

    auto pool = make_pool<sqlite>(":memory:", config);

    {
        auto conn = pool->acquire();
        ASSERT_TRUE(conn.is_valid());
        conn->close();
    }
    ASSERT_EQ(pool->size(), 0);
    ASSERT_EQ(pool->idle_count(), 0);

    {
        auto conn = pool->acquire();
        ASSERT_TRUE(conn.is_valid());
        EXPECT_TRUE(conn->is_open());
        EXPECT_EQ(pool->size(), 1);
        EXPECT_EQ(pool->active_count(), 1);
        EXPECT_EQ(pool->idle_count(), 0);
    }

    EXPECT_EQ(pool->size(), 1);
    EXPECT_EQ(pool->idle_count(), 1);
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

TEST(ConnectionPoolTest, TransactionDoubleCommitIsSafe) {
    auto conn = make_connection<sqlite>(":memory:");
    conn->open();

    Transaction txn(*conn);
    EXPECT_NO_THROW(txn.commit());
    EXPECT_NO_THROW(txn.commit()); // second commit should be a no-op
}

TEST(ConnectionPoolTest, InitializePropagatesExceptionOnBadConnectionString) {
    PoolConfig config;
    config.min_connections = 1;
    config.max_connections = 2;

    // A path in a non-existent directory cannot be opened by sqlite without parent directories
    EXPECT_THROW(
        make_pool<sqlite>("/non_existent_directory_987654321/db.sqlite", config),
        DbException
    );
}

TEST(ConnectionPoolTest, ConfigSanitization) {
    PoolConfig config;
    config.max_connections = 0;
    config.min_connections = 5;
    config.async_worker_count = 10;
    config.async_queue_capacity = 0;

    auto pool = make_pool<sqlite>(":memory:", config);
    EXPECT_EQ(pool->max_size(), 1);
    EXPECT_EQ(pool->size(), 1);
}

TEST(ConnectionPoolTest, PooledConnectionMoveSemanticsAndAccessors) {
    auto pool = make_pool<sqlite>(":memory:");
    auto conn = pool->acquire();
    EXPECT_TRUE(conn.is_valid());
    EXPECT_TRUE(static_cast<bool>(conn));
    EXPECT_NE(conn.operator->(), nullptr);
    EXPECT_NO_THROW((void)*conn);

    const auto& const_conn = conn;
    EXPECT_TRUE(const_conn.is_valid());
    EXPECT_TRUE(static_cast<bool>(const_conn));
    EXPECT_NE(const_conn.operator->(), nullptr);
    EXPECT_NO_THROW((void)*const_conn);
    EXPECT_NO_THROW((void)const_conn.get());

    // Move construct
    PooledConnection<sqlite> conn2(std::move(conn));
    EXPECT_FALSE(conn.is_valid());
    EXPECT_THROW(conn.get(), DbException);
    EXPECT_THROW((void)*conn, DbException);
    EXPECT_THROW((void)conn.operator->(), DbException);
    EXPECT_TRUE(conn2.is_valid());

    // Move assign
    PooledConnection<sqlite> conn3;
    EXPECT_FALSE(conn3.is_valid());
    conn3 = std::move(conn2);
    EXPECT_FALSE(conn2.is_valid());
    EXPECT_TRUE(conn3.is_valid());

    // Self-move assign
    conn3 = std::move(conn3);
    EXPECT_TRUE(conn3.is_valid());
}

TEST(ConnectionPoolTest, ReturnConnectionAfterClose) {
    auto pool = make_pool<sqlite>(":memory:");
    auto conn = pool->acquire();
    EXPECT_EQ(pool->active_count(), 1);

    pool->close();
    EXPECT_TRUE(pool->is_closed());

    // conn released after pool is closed
    EXPECT_NO_THROW(conn = PooledConnection<sqlite>{});
}

TEST(ConnectionPoolTest, AcquireWithPreCancelledStopTokenThrowsOperationCancelled) {
    auto pool = make_pool<sqlite>(":memory:");
    std::stop_source stop_source;
    stop_source.request_stop();

    EXPECT_THROW(pool->acquire(stop_source.get_token()), OperationCancelled);
}

TEST(ConnectionPoolTest, TryAcquireWithPreCancelledStopTokenThrowsOperationCancelled) {
    auto pool = make_pool<sqlite>(":memory:");
    std::stop_source stop_source;
    stop_source.request_stop();

    EXPECT_THROW(
        pool->try_acquire(std::chrono::milliseconds(50), stop_source.get_token()),
        OperationCancelled
    );
}

TEST(ConnectionPoolTest, TryAcquireCancelledWhileWaitingThrowsOperationCancelled) {
    PoolConfig config;
    config.min_connections = 1;
    config.max_connections = 1;

    auto pool = make_pool<sqlite>(":memory:", config);
    auto conn = pool->acquire(); // exhaust pool

    std::stop_source stop_source;
    std::atomic<bool> waiting{false};
    std::atomic<bool> threw_cancelled{false};

    std::thread waiter([&]() {
        waiting.store(true);
        try {
            pool->try_acquire(std::chrono::seconds(5), stop_source.get_token());
        } catch (const OperationCancelled&) {
            threw_cancelled.store(true);
        }
    });

    while (!waiting.load()) {
        std::this_thread::yield();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    stop_source.request_stop();
    waiter.join();

    EXPECT_TRUE(threw_cancelled.load());
}

TEST(ConnectionPoolTest, SubmitWithPreCancelledStopTokenThrowsOperationCancelled) {
    auto pool = make_pool<sqlite>(":memory:");
    std::stop_source stop_source;
    stop_source.request_stop();

    auto fut = pool->submit(stop_source.get_token(), [](auto&) {
        return 42;
    });
    EXPECT_THROW(fut.get(), OperationCancelled);
}

TEST(ConnectionPoolTest, TryAcquireWhenClosedReturnsNullopt) {
    auto pool = make_pool<sqlite>(":memory:");
    pool->close();

    auto conn = pool->try_acquire(std::chrono::milliseconds(50));
    EXPECT_FALSE(conn.has_value());
}

TEST(ConnectionPoolTest, TryAcquireClosedWhileWaitingReturnsNullopt) {
    PoolConfig config;
    config.min_connections = 1;
    config.max_connections = 1;

    auto pool = make_pool<sqlite>(":memory:", config);
    auto conn = pool->acquire(); // exhaust pool

    std::atomic<bool> waiting{false};
    std::atomic<bool> returned_nullopt{false};

    std::thread waiter([&]() {
        waiting.store(true);
        auto res = pool->try_acquire(std::chrono::seconds(5));
        if (!res.has_value()) {
            returned_nullopt.store(true);
        }
    });

    while (!waiting.load()) {
        std::this_thread::yield();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    pool->close();
    waiter.join();

    EXPECT_TRUE(returned_nullopt.load());
}

TEST(ConnectionPoolTest, TryAcquireSuccess) {
    PoolConfig config;
    config.min_connections = 1;
    config.max_connections = 2;

    auto pool = make_pool<sqlite>(":memory:", config);
    auto conn1 = pool->try_acquire(std::chrono::milliseconds(50));
    ASSERT_TRUE(conn1.has_value());
    EXPECT_TRUE(conn1->is_valid());

    auto conn2 = pool->try_acquire(std::chrono::milliseconds(50));
    ASSERT_TRUE(conn2.has_value());
    EXPECT_TRUE(conn2->is_valid());

    auto conn3 = pool->try_acquire(std::chrono::milliseconds(50));
    EXPECT_FALSE(conn3.has_value());
}

TEST(ConnectionPoolTest, AcquireTimeoutThrowsDbException) {
    PoolConfig config;
    config.min_connections = 1;
    config.max_connections = 1;
    config.acquire_timeout = std::chrono::milliseconds(50);

    auto pool = make_pool<sqlite>(":memory:", config);
    auto conn = pool->acquire();

    try {
        pool->acquire();
        FAIL() << "Expected DbException";
    } catch (const DbException& ex) {
        EXPECT_NE(std::string(ex.what()).find("Connection acquisition timed out"), std::string::npos);
    }
}

TEST(ConnectionPoolTest, AcquireClosedWhileWaitingThrowsDbException) {
    PoolConfig config;
    config.min_connections = 1;
    config.max_connections = 1;
    config.acquire_timeout = std::chrono::seconds(5);

    auto pool = make_pool<sqlite>(":memory:", config);
    auto conn = pool->acquire();

    std::atomic<bool> waiting{false};
    std::atomic<bool> threw_expected{false};

    std::thread waiter([&]() {
        waiting.store(true);
        try {
            pool->acquire();
        } catch (const DbException& ex) {
            if (std::string(ex.what()).find("ConnectionPool was closed while waiting") != std::string::npos) {
                threw_expected.store(true);
            }
        }
    });

    while (!waiting.load()) {
        std::this_thread::yield();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    pool->close();
    waiter.join();

    EXPECT_TRUE(threw_expected.load());
}

TEST(ConnectionPoolTest, CreateConnectionFailureRollsBackTotalConnections) {
    PoolConfig config;
    config.min_connections = 0;
    config.max_connections = 2;

    auto pool = make_pool<sqlite>("/non_existent_directory_987654321/db.sqlite", config);
    EXPECT_THROW(pool->acquire(), DbException);
    EXPECT_EQ(pool->size(), 0);

    EXPECT_THROW(pool->try_acquire(std::chrono::milliseconds(50)), DbException);
    EXPECT_EQ(pool->size(), 0);
}

