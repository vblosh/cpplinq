#pragma once

#include "cpplinq/driver/connection.h"
#include "cpplinq/driver/async_executor.h"
#include "cpplinq/core/db_context.h"
#include <chrono>
#include <condition_variable>
#include <exception>
#include <future>
#include <memory>
#include <mutex>
#include <vector>
#include <string>
#include <functional>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace cpplinq {

struct PoolConfig {
    size_t min_connections = 1;
    size_t max_connections = 10;
    std::chrono::milliseconds acquire_timeout{5000};
    size_t async_worker_count = 0;
    size_t async_queue_capacity = 64;
};

template <typename Backend>
class PooledConnection;

namespace detail {

template <typename Backend>
class ConnectionPoolState {
public:
    ConnectionPoolState(std::string connection_string, PoolConfig config)
        : connection_string_(std::move(connection_string)),
          config_(config) {
        if (config_.max_connections == 0) {
            config_.max_connections = 1;
        }
        if (config_.min_connections > config_.max_connections) {
            config_.min_connections = config_.max_connections;
        }
        if (config_.async_worker_count == 0 ||
            config_.async_worker_count > config_.max_connections) {
            config_.async_worker_count = config_.max_connections;
        }
        if (config_.async_queue_capacity == 0) {
            config_.async_queue_capacity = 1;
        }
    }

    void initialize();

    PooledConnection<Backend> acquire(
        const std::shared_ptr<ConnectionPoolState<Backend>>& self,
        std::optional<std::stop_token> stop_token = std::nullopt
    );

    std::optional<PooledConnection<Backend>> try_acquire(
        std::chrono::milliseconds timeout,
        const std::shared_ptr<ConnectionPoolState<Backend>>& self,
        std::optional<std::stop_token> stop_token = std::nullopt
    );

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return total_connections_;
    }

    size_t idle_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return idle_connections_.size();
    }

    size_t active_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return total_connections_ - idle_connections_.size();
    }

    bool is_closed() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return closed_;
    }

    const PoolConfig& config() const noexcept {
        return config_;
    }

    void close() {
        std::vector<std::unique_ptr<IConnection>> to_close;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_) return;
            closed_ = true;
            to_close = std::move(idle_connections_);
            total_connections_ -= to_close.size();
            condition_.notify_all();
        }
        for (auto& conn : to_close) {
            if (conn && conn->is_open()) {
                try { conn->close(); } catch(...) {}
            }
        }
    }

    void return_connection(std::unique_ptr<IConnection> conn) {
        if (!conn) return;

        bool need_close = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_ || !conn->is_open()) {
                total_connections_--;
                need_close = conn->is_open();
            } else {
                idle_connections_.push_back(std::move(conn));
            }
            condition_.notify_one();
        }

        if (need_close) {
            try { conn->close(); } catch(...) {}
        }
    }

private:
    std::unique_ptr<IConnection> create_new_connection() {
        auto conn = make_connection<Backend>(connection_string_);
        conn->open();
        return conn;
    }

    std::string connection_string_;
    PoolConfig config_;
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::vector<std::unique_ptr<IConnection>> idle_connections_;
    size_t total_connections_ = 0;
    bool closed_ = false;
};

} // namespace detail

template <typename Backend>
class PooledConnection {
public:
    PooledConnection() = default;

    PooledConnection(
        std::shared_ptr<detail::ConnectionPoolState<Backend>> state,
        std::unique_ptr<IConnection> conn
    ) : state_(std::move(state)), conn_(std::move(conn)) {}

    PooledConnection(const PooledConnection&) = delete;
    PooledConnection& operator=(const PooledConnection&) = delete;

    PooledConnection(PooledConnection&& other) noexcept
        : state_(std::move(other.state_)), conn_(std::move(other.conn_)) {}

    PooledConnection& operator=(PooledConnection&& other) noexcept {
        if (this != &other) {
            release();
            state_ = std::move(other.state_);
            conn_ = std::move(other.conn_);
        }
        return *this;
    }

    ~PooledConnection() {
        release();
    }

    IConnection& get() {
        if (!conn_) throw DbException("PooledConnection is invalid or has been moved");
        return *conn_;
    }
    const IConnection& get() const {
        if (!conn_) throw DbException("PooledConnection is invalid or has been moved");
        return *conn_;
    }

    IConnection* operator->() { return &get(); }
    const IConnection* operator->() const { return &get(); }

    IConnection& operator*() { return get(); }
    const IConnection& operator*() const { return get(); }

    bool is_valid() const noexcept { return conn_ != nullptr; }
    explicit operator bool() const noexcept { return is_valid(); }

    DbContext<Backend> get_context() {
        return DbContext<Backend>(get());
    }

private:
    void release();

    std::shared_ptr<detail::ConnectionPoolState<Backend>> state_;
    std::unique_ptr<IConnection> conn_;
};

namespace detail {

template <typename Backend>
PooledConnection<Backend> ConnectionPoolState<Backend>::acquire(
    const std::shared_ptr<ConnectionPoolState<Backend>>& self,
    std::optional<std::stop_token> stop_token
) {
    if (stop_token.has_value() && stop_token->stop_requested()) {
        throw OperationCancelled("Connection acquisition cancelled by stop token");
    }

    std::optional<std::stop_callback<std::function<void()>>> stop_cb;
    if (stop_token.has_value() && stop_token->stop_possible()) {
        stop_cb.emplace(*stop_token, [this]() {
            condition_.notify_all();
        });
    }

    std::unique_lock<std::mutex> lock(mutex_);
    if (closed_) {
        throw DbException("Cannot acquire connection: ConnectionPool is closed");
    }

    auto timeout = config_.acquire_timeout;
    bool success = condition_.wait_for(lock, timeout, [this, &stop_token]() {
        if (stop_token.has_value() && stop_token->stop_requested()) {
            return true;
        }
        return closed_ || !idle_connections_.empty() || total_connections_ < config_.max_connections;
    });

    if (stop_token.has_value() && stop_token->stop_requested()) {
        throw OperationCancelled("Connection acquisition cancelled by stop token");
    }

    if (closed_) {
        throw DbException("Cannot acquire connection: ConnectionPool was closed while waiting");
    }

    if (!success) {
        throw DbException("Connection acquisition timed out after " + std::to_string(timeout.count()) + "ms");
    }

    if (!idle_connections_.empty()) {
        auto conn = std::move(idle_connections_.back());
        idle_connections_.pop_back();
        return PooledConnection<Backend>(self, std::move(conn));
    }

    if (total_connections_ < config_.max_connections) {
        ++total_connections_;
        lock.unlock();
        try {
            auto conn = create_new_connection();
            return PooledConnection<Backend>(self, std::move(conn));
        } catch (...) {
            std::lock_guard<std::mutex> relock(mutex_);
            --total_connections_;
            condition_.notify_one();
            throw;
        }
    }

    throw DbException("Failed to acquire connection from pool");
}

template <typename Backend>
std::optional<PooledConnection<Backend>> ConnectionPoolState<Backend>::try_acquire(
    std::chrono::milliseconds timeout,
    const std::shared_ptr<ConnectionPoolState<Backend>>& self,
    std::optional<std::stop_token> stop_token
) {
    if (stop_token.has_value() && stop_token->stop_requested()) {
        throw OperationCancelled("Connection acquisition cancelled by stop token");
    }

    std::optional<std::stop_callback<std::function<void()>>> stop_cb;
    if (stop_token.has_value() && stop_token->stop_possible()) {
        stop_cb.emplace(*stop_token, [this]() {
            condition_.notify_all();
        });
    }

    std::unique_lock<std::mutex> lock(mutex_);
    if (closed_) return std::nullopt;

    bool success = condition_.wait_for(lock, timeout, [this, &stop_token]() {
        if (stop_token.has_value() && stop_token->stop_requested()) {
            return true;
        }
        return closed_ || !idle_connections_.empty() || total_connections_ < config_.max_connections;
    });

    if (stop_token.has_value() && stop_token->stop_requested()) {
        throw OperationCancelled("Connection acquisition cancelled by stop token");
    }

    if (closed_ || !success) return std::nullopt;

    if (!idle_connections_.empty()) {
        auto conn = std::move(idle_connections_.back());
        idle_connections_.pop_back();
        return PooledConnection<Backend>(self, std::move(conn));
    }

    if (total_connections_ < config_.max_connections) {
        ++total_connections_;
        lock.unlock();
        try {
            auto conn = create_new_connection();
            return PooledConnection<Backend>(self, std::move(conn));
        } catch (...) {
            std::lock_guard<std::mutex> relock(mutex_);
            --total_connections_;
            condition_.notify_one();
            throw;
        }
    }

    return std::nullopt;
}

template <typename Backend>
void ConnectionPoolState<Backend>::initialize() {
    std::vector<std::unique_ptr<IConnection>> created;
    created.reserve(config_.min_connections);
    for (size_t i = 0; i < config_.min_connections; ++i) {
        auto conn = create_new_connection();
        created.push_back(std::move(conn));
    }
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& conn : created) {
        idle_connections_.push_back(std::move(conn));
        total_connections_++;
    }
}

} // namespace detail

template <typename Backend>
class ConnectionPool : public std::enable_shared_from_this<ConnectionPool<Backend>> {
public:
    static std::shared_ptr<ConnectionPool<Backend>> create(
        std::string connection_string,
        PoolConfig config = PoolConfig{}
    ) {
        auto pool = std::shared_ptr<ConnectionPool<Backend>>(
            new ConnectionPool<Backend>(std::move(connection_string), config)
        );
        return pool;
    }

    ~ConnectionPool() {
        close();
    }

    ConnectionPool(const ConnectionPool&) = delete;
    ConnectionPool& operator=(const ConnectionPool&) = delete;

    PooledConnection<Backend> acquire(std::optional<std::stop_token> stop_token = std::nullopt) {
        return state_->acquire(state_, stop_token);
    }

    std::optional<PooledConnection<Backend>> try_acquire(
        std::chrono::milliseconds timeout,
        std::optional<std::stop_token> stop_token = std::nullopt
    ) {
        return state_->try_acquire(timeout, state_, stop_token);
    }

    template <typename Func>
    decltype(auto) with_connection(Func&& fn) {
        auto conn = acquire();
        return fn(*conn);
    }

    template <typename Func>
    decltype(auto) with_context(Func&& fn) {
        auto conn = acquire();
        auto ctx = conn.get_context();
        return fn(ctx);
    }

    size_t size() const {
        return state_->size();
    }

    size_t idle_count() const {
        return state_->idle_count();
    }

    size_t active_count() const {
        return state_->active_count();
    }

    size_t max_size() const noexcept {
        return state_->config().max_connections;
    }

    bool is_closed() const {
        return state_->is_closed();
    }

    void close() {
        std::lock_guard<std::mutex> close_lock(close_mutex_);
        state_->close();
        executor_.shutdown();
    }

    template <typename Func>
    auto submit(Func&& fn) {
        return submit_impl(std::nullopt, std::forward<Func>(fn));
    }

    template <typename Func>
    auto submit(std::stop_token stop_token, Func&& fn) {
        return submit_impl(stop_token, std::forward<Func>(fn));
    }

private:
    ConnectionPool(std::string connection_string, PoolConfig config)
        : state_(std::make_shared<detail::ConnectionPoolState<Backend>>(
              std::move(connection_string), config)),
          executor_(state_->config().async_worker_count,
                    state_->config().async_queue_capacity) {
        state_->initialize();
    }

    template <typename Func>
    auto submit_impl(std::optional<std::stop_token> stop_token, Func&& fn) {
        using Function = std::decay_t<Func>;
        using Result = std::invoke_result_t<Function&, DbContext<Backend>&>;
        using FutureResult = std::decay_t<Result>;

        static_assert(!std::is_reference_v<Result>,
                      "ConnectionPool::submit callbacks must return values, not references");

        std::lock_guard<std::mutex> close_lock(close_mutex_);
        if (state_->is_closed()) {
            std::promise<FutureResult> promise;
            auto future = promise.get_future();
            promise.set_exception(std::make_exception_ptr(
                DbException("Cannot submit asynchronous operation: ConnectionPool is closed")
            ));
            return future;
        }

        std::weak_ptr<detail::ConnectionPoolState<Backend>> weak_state = state_;
        return executor_.template submit<FutureResult>(
            [weak_state, stop_token, fn = std::forward<Func>(fn)]() mutable -> FutureResult {
                if (stop_token.has_value() && stop_token->stop_requested()) {
                    throw OperationCancelled("Asynchronous operation was cancelled before it started");
                }

                auto state = weak_state.lock();
                if (!state) {
                    throw DbException("Asynchronous operation cannot acquire its connection pool");
                }

                auto conn = state->acquire(state, stop_token);
                if (stop_token.has_value() && stop_token->stop_requested()) {
                    throw OperationCancelled("Asynchronous operation was cancelled before it started");
                }

                auto db = conn.get_context();
                if constexpr (std::is_void_v<FutureResult>) {
                    std::invoke(fn, db);
                } else {
                    return std::invoke(fn, db);
                }
            }
        );
    }

    std::shared_ptr<detail::ConnectionPoolState<Backend>> state_;
    detail::BoundedTaskExecutor executor_;
    std::mutex close_mutex_;
};

template <typename Backend>
void PooledConnection<Backend>::release() {
    if (conn_ && state_) {
        state_->return_connection(std::move(conn_));
        conn_.reset();
        state_.reset();
    }
}

// Convenience factory function
template <typename Backend>
std::shared_ptr<ConnectionPool<Backend>> make_pool(
    std::string connection_string,
    PoolConfig config = PoolConfig{}
) {
    return ConnectionPool<Backend>::create(std::move(connection_string), config);
}

} // namespace cpplinq
