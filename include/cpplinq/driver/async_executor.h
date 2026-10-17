#pragma once

#include "cpplinq/driver/connection.h"
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace cpplinq::detail {

class BoundedTaskExecutor {
private:
    class Job {
    public:
        virtual ~Job() = default;
        virtual void run() noexcept = 0;
        virtual void cancel() noexcept = 0;
        virtual void fail(std::exception_ptr error) noexcept = 0;
    };

    template <typename Result, typename Func>
    class TypedJob final : public Job {
    public:
        TypedJob(std::shared_ptr<std::promise<Result>> promise, Func&& fn)
            : promise_(std::move(promise)), fn_(std::forward<Func>(fn)) {}

        void run() noexcept override {
            try {
                if constexpr (std::is_void_v<Result>) {
                    std::invoke(fn_);
                    promise_->set_value();
                } else {
                    promise_->set_value(std::invoke(fn_));
                }
            } catch (...) {
                fail(std::current_exception());
            }
        }

        void cancel() noexcept override {
            fail(std::make_exception_ptr(
                OperationCancelled("Asynchronous operation was cancelled before it started")
            ));
        }

        void fail(std::exception_ptr error) noexcept override {
            try {
                promise_->set_exception(std::move(error));
            } catch (...) {
                // The future may already have been satisfied by an exceptional shutdown race.
            }
        }

    private:
        std::shared_ptr<std::promise<Result>> promise_;
        Func fn_;
    };

    struct State {
        std::mutex mutex;
        std::condition_variable condition;
        std::deque<std::unique_ptr<Job>> queue;
        bool stopping = false;
    };

public:
    BoundedTaskExecutor(size_t worker_count, size_t queue_capacity)
        : worker_count_(worker_count == 0 ? 1 : worker_count),
          queue_capacity_(queue_capacity == 0 ? 1 : queue_capacity),
          state_(std::make_shared<State>()) {}

    BoundedTaskExecutor(const BoundedTaskExecutor&) = delete;
    BoundedTaskExecutor& operator=(const BoundedTaskExecutor&) = delete;

    ~BoundedTaskExecutor() {
        shutdown();
    }

    template <typename Result, typename Func>
    std::future<Result> submit(Func&& fn) {
        auto promise = std::make_shared<std::promise<Result>>();
        auto future = promise->get_future();
        auto job = std::make_unique<TypedJob<Result, std::decay_t<Func>>>(
            promise, std::forward<Func>(fn)
        );

        {
            std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
            std::lock_guard<std::mutex> state_lock(state_->mutex);

            if (state_->stopping) {
                job->fail(std::make_exception_ptr(
                    DbException("Cannot submit asynchronous operation: executor is shut down")
                ));
                return future;
            }

            if (state_->queue.size() >= queue_capacity_) {
                job->fail(std::make_exception_ptr(
                    DbException("Cannot submit asynchronous operation: executor queue is full")
                ));
                return future;
            }

            try {
                start_workers_locked();
            } catch (...) {
                job->fail(std::current_exception());
                return future;
            }

            state_->queue.push_back(std::move(job));
        }
        state_->condition.notify_one();
        return future;
    }

    void shutdown() noexcept {
        std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
        std::deque<std::unique_ptr<Job>> queued_jobs;
        {
            std::lock_guard<std::mutex> state_lock(state_->mutex);
            state_->stopping = true;
            queued_jobs.swap(state_->queue);
        }

        for (auto& job : queued_jobs) {
            if (job) job->cancel();
        }
        state_->condition.notify_all();

        const auto caller = std::this_thread::get_id();
        for (auto& worker : workers_) {
            if (!worker.joinable()) continue;
            if (worker.get_id() == caller) {
                worker.detach();
            } else {
                worker.join();
            }
        }
    }

private:
    static void run_worker(std::shared_ptr<State> state) noexcept {
        for (;;) {
            std::unique_ptr<Job> job;
            {
                std::unique_lock<std::mutex> lock(state->mutex);
                state->condition.wait(lock, [&state]() {
                    return state->stopping || !state->queue.empty();
                });

                if (state->queue.empty()) {
                    if (state->stopping) return;
                    continue;
                }

                job = std::move(state->queue.front());
                state->queue.pop_front();
            }
            if (job) job->run();
        }
    }

    void start_workers_locked() {
        while (workers_.size() < worker_count_) {
            workers_.emplace_back([state = state_]() mutable {
                run_worker(std::move(state));
            });
        }
    }

    size_t worker_count_;
    size_t queue_capacity_;
    std::shared_ptr<State> state_;
    std::mutex lifecycle_mutex_;
    std::vector<std::thread> workers_;
};

} // namespace cpplinq::detail
