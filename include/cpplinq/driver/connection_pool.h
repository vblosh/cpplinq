#pragma once

#include "cpplinq/driver/connection.h"
#include "cpplinq/core/db_context.h"
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <vector>
#include <string>
#include <functional>
#include <optional>
#include <stdexcept>

namespace cpplinq {

struct PoolConfig {
    size_t min_connections = 1;
    size_t max_connections = 10;
    std::chrono::milliseconds acquire_timeout{5000};
};

template <typename Backend>
class ConnectionPool;

template <typename Backend>
class PooledConnection {
public:
    PooledConnection() = default;

    PooledConnection(std::shared_ptr<ConnectionPool<Backend>> pool, std::unique_ptr<IConnection> conn)
        : pool_(std::move(pool)), conn_(std::move(conn)) {}

    PooledConnection(const PooledConnection&) = delete;
    PooledConnection& operator=(const PooledConnection&) = delete;

    PooledConnection(PooledConnection&& other) noexcept
        : pool_(std::move(other.pool_)), conn_(std::move(other.conn_)) {}

    PooledConnection& operator=(PooledConnection&& other) noexcept {
        if (this != &other) {
            release();
            pool_ = std::move(other.pool_);
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

    std::shared_ptr<ConnectionPool<Backend>> pool_;
    std::unique_ptr<IConnection> conn_;
};

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
        pool->initialize();
        return pool;
    }

    ~ConnectionPool() {
        close();
    }

    ConnectionPool(const ConnectionPool&) = delete;
    ConnectionPool& operator=(const ConnectionPool&) = delete;

    PooledConnection<Backend> acquire() {
        std::unique_lock<std::mutex> lock(mutex_);
        if (closed_) {
            throw DbException("Cannot acquire connection: ConnectionPool is closed");
        }

        auto timeout = config_.acquire_timeout;
        bool success = cv_.wait_for(lock, timeout, [this]() {
            return closed_ || !idle_connections_.empty() || total_connections_ < config_.max_connections;
        });

        if (closed_) {
            throw DbException("Cannot acquire connection: ConnectionPool was closed while waiting");
        }

        if (!success) {
            throw DbException("Connection acquisition timed out after " + std::to_string(timeout.count()) + "ms");
        }

        if (!idle_connections_.empty()) {
            auto conn = std::move(idle_connections_.back());
            idle_connections_.pop_back();
            return PooledConnection<Backend>(this->shared_from_this(), std::move(conn));
        }

        if (total_connections_ < config_.max_connections) {
            auto conn = create_new_connection();
            total_connections_++;
            return PooledConnection<Backend>(this->shared_from_this(), std::move(conn));
        }

        throw DbException("Failed to acquire connection from pool");
    }

    std::optional<PooledConnection<Backend>> try_acquire(std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (closed_) return std::nullopt;

        bool success = cv_.wait_for(lock, timeout, [this]() {
            return closed_ || !idle_connections_.empty() || total_connections_ < config_.max_connections;
        });

        if (closed_ || !success) return std::nullopt;

        if (!idle_connections_.empty()) {
            auto conn = std::move(idle_connections_.back());
            idle_connections_.pop_back();
            return PooledConnection<Backend>(this->shared_from_this(), std::move(conn));
        }

        if (total_connections_ < config_.max_connections) {
            auto conn = create_new_connection();
            total_connections_++;
            return PooledConnection<Backend>(this->shared_from_this(), std::move(conn));
        }

        return std::nullopt;
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

    size_t max_size() const noexcept {
        return config_.max_connections;
    }

    bool is_closed() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return closed_;
    }

    void close() {
        std::vector<std::unique_ptr<IConnection>> to_close;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_) return;
            closed_ = true;
            to_close = std::move(idle_connections_);
            total_connections_ -= to_close.size();
            cv_.notify_all();
        }
        for (auto& conn : to_close) {
            if (conn && conn->is_open()) {
                try { conn->close(); } catch(...) {}
            }
        }
    }

    void return_connection(std::unique_ptr<IConnection> conn) {
        if (!conn) return;

        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_ || !conn->is_open()) {
            total_connections_--;
            try { if (conn->is_open()) conn->close(); } catch(...) {}
        } else {
            idle_connections_.push_back(std::move(conn));
        }
        cv_.notify_one();
    }

private:
    ConnectionPool(std::string connection_string, PoolConfig config)
        : connection_string_(std::move(connection_string)),
          config_(config) {
        if (config_.max_connections == 0) {
            config_.max_connections = 1;
        }
        if (config_.min_connections > config_.max_connections) {
            config_.min_connections = config_.max_connections;
        }
    }

    void initialize() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (size_t i = 0; i < config_.min_connections; ++i) {
            try {
                auto conn = create_new_connection();
                idle_connections_.push_back(std::move(conn));
                total_connections_++;
            } catch (...) {
                // If initial creation fails, proceed with available connections
                break;
            }
        }
    }

    std::unique_ptr<IConnection> create_new_connection() {
        auto conn = make_connection<Backend>(connection_string_);
        conn->open();
        return conn;
    }

    std::string connection_string_;
    PoolConfig config_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<std::unique_ptr<IConnection>> idle_connections_;
    size_t total_connections_ = 0;
    bool closed_ = false;
};

template <typename Backend>
void PooledConnection<Backend>::release() {
    if (conn_ && pool_) {
        pool_->return_connection(std::move(conn_));
        conn_.reset();
        pool_.reset();
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
