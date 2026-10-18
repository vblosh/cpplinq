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

TEST(AsyncConnectionPoolTest, CompletesRunningCallbackWhenStopRequestedAfterStart) {
    auto pool = make_pool<sqlite>(make_database_path(), single_worker_config());
    std::latch started(1);
    std::latch release(1);
    std::stop_source stop_source;

    auto future = pool->submit(stop_source.get_token(), [&](auto&) {
        // Reaching this point means the worker passed the final cancellation
        // check immediately before invoking the callback.
        started.count_down();
        release.wait();
        return 23;
    });

    started.wait();
    stop_source.request_stop();
    release.count_down();

    EXPECT_EQ(future.get(), 23);
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

TEST(BoundedTaskExecutorTest, DefaultsZeroWorkersAndCapacityToOne) {
    cpplinq::detail::BoundedTaskExecutor executor(0, 0);
    auto fut = executor.submit<int>([]() { return 42; });
    EXPECT_EQ(fut.get(), 42);
}

TEST(BoundedTaskExecutorTest, SubmitVoidAndExceptionHandling) {
    cpplinq::detail::BoundedTaskExecutor executor(1, 2);

    std::atomic<bool> void_called{false};
    auto void_fut = executor.submit<void>([&]() {
        void_called.store(true);
    });
    EXPECT_NO_THROW(void_fut.get());
    EXPECT_TRUE(void_called.load());

    auto void_err_fut = executor.submit<void>([]() {
        throw std::runtime_error("void error");
    });
    EXPECT_THROW(void_err_fut.get(), std::runtime_error);

    auto int_err_fut = executor.submit<int>([]() -> int {
        throw std::runtime_error("int error");
    });
    EXPECT_THROW(int_err_fut.get(), std::runtime_error);
}

TEST(BoundedTaskExecutorTest, QueueFullThrowsDbException) {
    cpplinq::detail::BoundedTaskExecutor executor(1, 1);
    std::latch running(1);
    std::latch release(1);

    auto fut1 = executor.submit<int>([&]() {
        running.count_down();
        release.wait();
        return 1;
    });
    running.wait();

    auto fut2 = executor.submit<int>([]() {
        return 2;
    });
    auto fut3 = executor.submit<int>([]() {
        return 3;
    });

    try {
        fut3.get();
        FAIL() << "Expected DbException";
    } catch (const DbException& ex) {
        EXPECT_STREQ(ex.what(), "Cannot submit asynchronous operation: executor queue is full");
    }

    release.count_down();
    EXPECT_EQ(fut1.get(), 1);
    EXPECT_EQ(fut2.get(), 2);

    // Also test submit<void> when queue is full
    cpplinq::detail::BoundedTaskExecutor executor_void(1, 1);
    std::latch running_v(1);
    std::latch release_v(1);
    auto fv1 = executor_void.submit<void>([&]() {
        running_v.count_down();
        release_v.wait();
    });
    running_v.wait();

    auto fv2 = executor_void.submit<void>([]() {});
    auto fv3 = executor_void.submit<void>([]() {});

    try {
        fv3.get();
        FAIL() << "Expected DbException";
    } catch (const DbException& ex) {
        EXPECT_STREQ(ex.what(), "Cannot submit asynchronous operation: executor queue is full");
    }

    release_v.count_down();
    EXPECT_NO_THROW(fv1.get());
    EXPECT_NO_THROW(fv2.get());
}

TEST(BoundedTaskExecutorTest, SubmitAfterShutdownThrowsDbException) {
    cpplinq::detail::BoundedTaskExecutor executor(1, 2);
    executor.shutdown();

    auto int_fut = executor.submit<int>([]() { return 10; });
    try {
        int_fut.get();
        FAIL() << "Expected DbException";
    } catch (const DbException& ex) {
        EXPECT_STREQ(ex.what(), "Cannot submit asynchronous operation: executor is shut down");
    }

    auto void_fut = executor.submit<void>([]() {});
    try {
        void_fut.get();
        FAIL() << "Expected DbException";
    } catch (const DbException& ex) {
        EXPECT_STREQ(ex.what(), "Cannot submit asynchronous operation: executor is shut down");
    }
}

TEST(BoundedTaskExecutorTest, ShutdownCancelsQueuedJobs) {
    cpplinq::detail::BoundedTaskExecutor executor(1, 2);
    std::latch running(1);
    std::latch release(1);

    auto fut1 = executor.submit<int>([&]() {
        running.count_down();
        release.wait();
        return 1;
    });
    running.wait();

    auto fut2 = executor.submit<int>([]() { return 2; });
    auto fut3 = executor.submit<void>([]() {});

    std::thread shutdown_thread([&]() {
        executor.shutdown();
    });

    // fut2 gets cancelled during shutdown before shutdown waits for worker.join()
    fut2.wait();
    release.count_down();
    shutdown_thread.join();

    EXPECT_EQ(fut1.get(), 1);
    EXPECT_THROW(fut2.get(), OperationCancelled);
    EXPECT_THROW(fut3.get(), OperationCancelled);
}

TEST(BoundedTaskExecutorTest, WorkerCallingShutdownDetachesSelfAndJoinsOthers) {
    cpplinq::detail::BoundedTaskExecutor executor(2, 4);
    auto fut = executor.submit<int>([&executor]() {
        executor.shutdown();
        return 99;
    });
    EXPECT_EQ(fut.get(), 99);
}

TEST(AsyncConnectionPoolTest, SubmitVoidReturningLambda) {
    const auto database_path = make_database_path();
    auto pool = make_pool<sqlite>(database_path, single_worker_config());

    std::atomic<bool> invoked{false};
    auto future = pool->submit([&](auto& db) {
        db.ensure_table(async_items_table);
        invoked.store(true);
    });
    EXPECT_NO_THROW(future.get());
    EXPECT_TRUE(invoked.load());

    pool->close();
    std::filesystem::remove(database_path);
}

TEST(AsyncConnectionPoolTest, SubmitThrowsStdException) {
    auto pool = make_pool<sqlite>(make_database_path(), single_worker_config());

    auto int_future = pool->submit([](auto&) -> int {
        throw std::runtime_error("boom");
    });
    EXPECT_THROW(int_future.get(), std::runtime_error);

    auto void_future = pool->submit([](auto&) {
        throw std::runtime_error("void boom");
    });
    EXPECT_THROW(void_future.get(), std::runtime_error);

    pool->close();
}

TEST(AsyncConnectionPoolTest, SubmitAfterCloseRejectsWithExactMessage) {
    auto pool = make_pool<sqlite>(make_database_path(), single_worker_config());
    pool->close();

    auto int_fut = pool->submit([](auto&) { return 1; });
    try {
        int_fut.get();
        FAIL() << "Expected DbException";
    } catch (const DbException& ex) {
        EXPECT_STREQ(ex.what(), "Cannot submit asynchronous operation: ConnectionPool is closed");
    }

    auto void_fut = pool->submit([](auto&) {});
    try {
        void_fut.get();
        FAIL() << "Expected DbException";
    } catch (const DbException& ex) {
        EXPECT_STREQ(ex.what(), "Cannot submit asynchronous operation: ConnectionPool is closed");
    }
}

TEST(AsyncConnectionPoolTest, PoolClosedWhileSubmitWaitingForConnection) {
    PoolConfig config;
    config.min_connections = 1;
    config.max_connections = 1;
    config.async_worker_count = 1;
    config.async_queue_capacity = 4;
    config.acquire_timeout = std::chrono::seconds(5);

    auto pool = make_pool<sqlite>(make_database_path(), config);
    auto conn = pool->acquire();

    auto fut = pool->submit([](auto&) { return 42; });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    pool->close();

    EXPECT_THROW(fut.get(), OperationCancelled);
}

