# Thread-Safe Connection Pooling Guide

A **Connection Pool** manages a cache of reusable, pre-warmed physical database connections. It is an essential component for high-throughput, multi-threaded server applications that interact with relational databases.

---

## 1. Why Connection Pools Are Essential

Establishing a physical database connection over the network is one of the most resource-intensive operations in software development:

- **TCP 3-Way Handshakes**: Network round-trips incurred for every connection.
- **TLS / SSL Cryptographic Negotiation**: Symmetric key exchange and certificate validation.
- **Authentication & Handshake**: User authentication, password hashing, and server-side session allocation.
- **Database Server Memory & CPU**: Relational engines (PostgreSQL, MySQL, SQL Server) allocate dedicated memory buffers and OS worker threads for every open connection.

Without a pool, under high concurrency (e.g., hundreds of concurrent API requests), opening and closing connections per request causes connection exhaustion, port exhaustion, high latency spikes, and server thrashing.

---

## 2. Architecture & Design in `cpplinq`

`cpplinq` provides a generic, type-safe, thread-safe connection pool via `ConnectionPool<Backend>` in [`include/cpplinq/driver/connection_pool.h`](../include/cpplinq/driver/connection_pool.h).

```
                           ┌────────────────────────┐
                           │ ConnectionPool<Backend>│
                           └───────────┬────────────┘
               acquire()               │               release() (automatic on scope exit)
     ┌─────────────────────────◄───────┴────────►─────────────────────────┐
     │                                                                    │
┌────┴──────────────┐                                            ┌────────┴──────────┐
│ Thread 1 (Worker) │ ──── uses PooledConnection<Backend> ────► │ Idle Connections  │
└───────────────────┘                                            │  [c1]  [c2]  [c3] │
┌───────────────────┐                                            └───────────────────┘
│ Thread 2 (Worker) │ ──── uses PooledConnection<Backend> ────►
└───────────────────┘
```

### Core Components

1. **`ConnectionPool<Backend>`**:
   The central thread-safe manager maintaining active and idle connection queues, controlling minimum/maximum capacity, and handling thread synchronization.
2. **`PooledConnection<Backend>`**:
   An RAII smart handle wrapping an active `IConnection`. When the object goes out of scope, its destructor automatically returns the physical connection to the idle pool.
3. **`PoolConfig`**:
   Configuration struct specifying pool sizing and timeout thresholds.

---

## 3. Configuration Options

| Option | Type | Default | Description |
|---|---|---|---|
| `min_connections` | `size_t` | `1` | Number of idle connections created and kept alive immediately upon pool initialization. |
| `max_connections` | `size_t` | `10` | Hard upper bound on total open connections (active + idle). |
| `acquire_timeout` | `std::chrono::milliseconds` | `5000ms` (5s) | Maximum duration a thread will wait for an available connection before throwing a `DbException`. |

---

## 4. Key Mechanisms

### 4.1 RAII-Guarded Leasing (Zero Connection Leaks)
`pool->acquire()` returns a `PooledConnection` object:

```cpp
{
    auto conn = pool->acquire();
    auto db = conn.get_context(); // Or DbContext<Backend>(conn.get())
    auto users = db.from(users_table).to_list();
    
    // As soon as `conn` goes out of scope, its destructor returns the
    // physical connection back to the idle queue automatically.
}
```

Even if an exception is thrown inside the block, C++ stack unwinding guarantees that the connection is cleanly returned to the pool without leaking.

### 4.2 Dynamic Sizing & Thread Synchronization
- **Idle Reuse**: If an idle connection is available in `idle_connections_`, `acquire()` returns it immediately with sub-microsecond latency.
- **Dynamic Expansion**: If all idle connections are leased but `total_connections_ < max_connections`, a new connection is spawned on-demand.
- **Wait with Condition Variable**: If all `max_connections` are currently active, acquiring threads safely sleep on `std::condition_variable` until another thread releases a connection or the timeout expires.

### 4.3 Health Check & Auto-Recovery
Before returning a connection to a caller, the pool validates `conn->is_open()`. If a connection was dropped due to a server restart or network interruption, it is discarded and replaced with a healthy instance automatically.

---

## 5. Complete Multi-Threaded Example

```cpp
#include <iostream>
#include <vector>
#include <thread>
#include <cpplinq/cpplinq.hpp>
#include <cpplinq/driver/connection_pool.h>

using namespace cpplinq;

// Entity definition
struct User {
    int id = 0;
    std::string name;
    std::optional<std::string> email;
    int age = 0;
};

inline const auto users_table = table<User>(
    "users",
    column("id",    &User::id,    primary_key, auto_increment),
    column("name",  &User::name,  not_null),
    column("email", &User::email),
    column("age",   &User::age,   not_null)
);

int main() {
    // 1. Configure pool
    PoolConfig config;
    config.min_connections = 2;               // Keep 2 pre-warmed idle connections
    config.max_connections = 8;               // Scale up to 8 concurrent connections
    config.acquire_timeout = std::chrono::seconds(3); // 3-second timeout

    // 2. Instantiate the pool (works with sqlite, postgres, mssql, mysql)
    auto pool = ConnectionPool<mysql>::create("MySQLtestdb", config);

    // 3. Initialize schema using a leased connection
    {
        auto conn = pool->acquire();
        auto db = conn.get_context();
        db.ensure_table(users_table);
    }

    // 4. Launch concurrent worker threads
    std::vector<std::thread> workers;
    for (int i = 0; i < 16; ++i) {
        workers.emplace_back([pool, i]() {
            try {
                // Thread-safe lease with RAII guard
                auto conn = pool->acquire();
                auto db = conn.get_context();

                // Insert record
                db.insert(users_table, User{0, "Worker_" + std::to_string(i), std::nullopt, 20 + i});

                // Query records
                auto count = db.from(users_table).count();
                std::cout << "[Thread " << std::this_thread::get_id() << "] Total users: " << count << "\n";

            } catch (const std::exception& ex) {
                std::cerr << "[Thread Error] " << ex.what() << "\n";
            }
        });
    }

    // 5. Join worker threads
    for (auto& worker : workers) {
        worker.join();
    }

    // 6. Inspect pool metrics
    std::cout << "Active: " << pool->active_count() 
              << ", Idle: " << pool->idle_count() 
              << ", Total: " << pool->total_count() << "\n";

    return 0;
}
```

---

## 6. Best Practices

1. **Keep Lease Durations Short**: Only hold a `PooledConnection` while actively querying. Do not perform long-running CPU calculations, file I/O, or external network requests while holding a connection lease.
2. **Handle Exceptions Gracefully**: Catch `DbException` around `pool->acquire()` to detect when database connection limits or timeouts have been reached.
3. **Transaction Boundaries**: If using `db.begin_transaction()`, always commit or rollback within the scope of the same `PooledConnection` before letting it return to the pool.
4. **Pool Sizing Guideline**:
   $$\text{Optimal Pool Size} = 2 \times \text{CPU Cores} + \text{Effective Spindle / SSD Count}$$
   Over-allocating connections beyond database server capacity leads to thread scheduling contention and decreased overall throughput.
