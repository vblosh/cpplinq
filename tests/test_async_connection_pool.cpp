#include <gtest/gtest.h>
#include "cpplinq/cpplinq.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <latch>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace cpplinq;

namespace {

struct AsyncItem {
    int64_t id = 0;
    std::string name;
    int value = 0;
};

inline const auto async_items_table = table<AsyncItem>(
    "test_async_items",
    column("id", &AsyncItem::id, primary_key, auto_increment),
    column("name", &AsyncItem::name),
    column("value", &AsyncItem::value)
);

std::string make_database_path() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return (std::filesystem::temp_directory_path() /
            ("cpplinq_async_" + std::to_string(stamp) + ".db")).string();
}

PoolConfig single_worker_config(size_t queue_capacity = 4) {
    PoolConfig config;
    config.min_connections = 1;
    config.max_connections = 1;
    config.async_worker_count = 1;
    config.async_queue_capacity = queue_capacity;
    config.acquire_timeout = std::chrono::seconds(2);
    return config;
}

} // namespace

TEST(AsyncConnectionPoolTest, SubmitsReadsWritesBatchesAndMoveOnlyResults) {
    const auto database_path = make_database_path();
    auto pool = make_pool<sqlite>(database_path, single_worker_config());

    {
        auto connection = pool->acquire();
        auto db = connection.get_context();
        db.ensure_table(async_items_table);
    }

    auto insert_future = pool->submit([](auto& db) {
        return db.insert(async_items_table, AsyncItem{0, "first", 10});
    });
    EXPECT_GT(insert_future.get(), 0);

    auto batch_future = pool->submit([](auto& db) {
        db.insert_many(async_items_table, std::vector<AsyncItem>{
            {0, "second", 20},
            {0, "third", 30}
        });
    });
    EXPECT_NO_THROW(batch_future.get());

    auto read_future = pool->submit([](auto& db) {
        return db.from(async_items_table)
            .order_by(async_items_table["value"])
            .to_list()
            .to_vector();
    });
    const auto rows = read_future.get();
    ASSERT_EQ(rows.size(), 3);
    EXPECT_EQ(rows[0].name, "first");
    EXPECT_EQ(rows[2].value, 30);

    auto move_only_future = pool->submit([](auto&) {
        return std::make_unique<int>(42);
    });
    auto move_only_result = move_only_future.get();
    ASSERT_NE(move_only_result, nullptr);
    EXPECT_EQ(*move_only_result, 42);
    EXPECT_EQ(pool->active_count(), 0);

    pool->close();
    std::filesystem::remove(database_path);
}

TEST(AsyncConnectionPoolTest, PropagatesExceptionsAndReturnsLease) {
    auto pool = make_pool<sqlite>(make_database_path(), single_worker_config());

    auto failing_future = pool->submit([](auto&) -> int {
        throw DbException("async callback failed");
    });
    EXPECT_THROW(failing_future.get(), DbException);
    EXPECT_EQ(pool->active_count(), 0);

    auto succeeding_future = pool->submit([](auto&) {
        return 7;
    });
    EXPECT_EQ(succeeding_future.get(), 7);
    EXPECT_EQ(pool->active_count(), 0);

    pool->close();
}

TEST(AsyncConnectionPoolTest, RejectsSubmissionsWhenQueueIsFull) {
    auto pool = make_pool<sqlite>(make_database_path(), single_worker_config(1));
    std::latch started(1);
    std::latch release(1);

    auto running = pool->submit([&](auto&) {
        started.count_down();
        release.wait();
        return 1;
    });
    started.wait();

    auto queued = pool->submit([](auto&) {
        return 2;
    });
    auto rejected = pool->submit([](auto&) {
        return 3;
    });

    EXPECT_THROW(rejected.get(), DbException);
    release.count_down();
    EXPECT_EQ(running.get(), 1);
    EXPECT_EQ(queued.get(), 2);
    EXPECT_EQ(pool->active_count(), 0);

    pool->close();
}

TEST(AsyncConnectionPoolTest, CancelsQueuedOperationBeforeCallbackStarts) {
    auto pool = make_pool<sqlite>(make_database_path(), single_worker_config(1));
    std::latch started(1);
    std::latch release(1);
    std::atomic<bool> invoked{false};
    std::stop_source stop_source;

    auto running = pool->submit([&](auto&) {
        started.count_down();
        release.wait();
        return 1;
    });
    started.wait();

    auto queued = pool->submit(stop_source.get_token(), [&](auto&) {
        invoked.store(true, std::memory_order_relaxed);
        return 2;
    });
    stop_source.request_stop();
    release.count_down();

    EXPECT_EQ(running.get(), 1);
    EXPECT_THROW(queued.get(), OperationCancelled);
    EXPECT_FALSE(invoked.load(std::memory_order_relaxed));
    EXPECT_EQ(pool->active_count(), 0);

    pool->close();
}

TEST(AsyncConnectionPoolTest, CancelsWaitingAcquisitionWhenStopTokenTriggered) {
    PoolConfig config;
    config.min_connections = 1;
    config.max_connections = 1;
    config.acquire_timeout = std::chrono::seconds(10);

    auto pool = make_pool<sqlite>(make_database_path(), config);
    // Exhaust the 1 available connection
    auto conn = pool->acquire();
    EXPECT_TRUE(conn.is_valid());

    std::stop_source stop_source;
    std::atomic<bool> thread_started{false};
    std::atomic<bool> threw_cancelled{false};

    std::thread waiter([&]() {
        thread_started.store(true);
        try {
            pool->acquire(stop_source.get_token());
        } catch (const OperationCancelled&) {
            threw_cancelled.store(true);
        }
    });

    while (!thread_started.load()) {
        std::this_thread::yield();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    auto t0 = std::chrono::steady_clock::now();
    stop_source.request_stop();

    waiter.join();
    auto elapsed = std::chrono::steady_clock::now() - t0;

    EXPECT_TRUE(threw_cancelled.load());
    EXPECT_LT(elapsed, std::chrono::seconds(2));

    pool->close();
}

TEST(AsyncConnectionPoolTest, WorkerCanClosePoolWithoutSelfJoin) {
    auto pool = make_pool<sqlite>(make_database_path(), single_worker_config());
    auto close_future = pool->submit([pool](auto&) {
        pool->close();
        return 11;
    });

    EXPECT_EQ(close_future.get(), 11);
    EXPECT_TRUE(pool->is_closed());
    EXPECT_THROW(pool->acquire(), DbException);
}

TEST(AsyncConnectionPoolTest, RejectsSubmissionAfterClose) {
    auto pool = make_pool<sqlite>(make_database_path(), single_worker_config());
    pool->close();

    auto future = pool->submit([](auto&) {
        return 1;
    });
    EXPECT_THROW(future.get(), DbException);
}

TEST(AsyncConnectionPoolTest, PoolDestructionWaitsForRunningFuture) {
    std::latch started(1);
    std::latch release(1);
    std::future<int> future;

    std::thread finisher([&]() {
        started.wait();
        release.count_down();
    });

    {
        auto pool = make_pool<sqlite>(make_database_path(), single_worker_config());
        future = pool->submit([&](auto&) {
            started.count_down();
            release.wait();
            return 17;
        });
        started.wait();
    }

    finisher.join();
    EXPECT_EQ(future.get(), 17);
}

TEST(AsyncConnectionPoolTest, CloseCancelsQueuedAndFinishesRunningWork) {
    auto pool = make_pool<sqlite>(make_database_path(), single_worker_config(1));
    std::latch started(1);
    std::latch release(1);
    std::atomic<bool> queued_invoked{false};

    auto running = pool->submit([&](auto&) {
        started.count_down();
        release.wait();
        return 1;
    });
    started.wait();
    auto queued = pool->submit([&](auto&) {
        queued_invoked.store(true, std::memory_order_relaxed);
        return 2;
    });

    std::mutex close_mutex;
    std::condition_variable close_condition;
    bool close_started = false;
    bool close_finished = false;
    std::thread closer([&]() {
        {
            std::lock_guard<std::mutex> lock(close_mutex);
            close_started = true;
        }
        close_condition.notify_one();
        pool->close();
        {
            std::lock_guard<std::mutex> lock(close_mutex);
            close_finished = true;
        }
        close_condition.notify_one();
    });

    {
        std::unique_lock<std::mutex> lock(close_mutex);
        close_condition.wait(lock, [&]() { return close_started; });
    }
    while (!pool->is_closed()) {
        std::this_thread::yield();
    }
    release.count_down();
    closer.join();

    EXPECT_EQ(running.get(), 1);
    EXPECT_THROW(queued.get(), OperationCancelled);
    EXPECT_FALSE(queued_invoked.load(std::memory_order_relaxed));
    EXPECT_TRUE(pool->is_closed());
    EXPECT_TRUE(close_finished);
}
