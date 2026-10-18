# Asynchronous Operations

`cpplinq` provides pool-scoped asynchronous submission for applications that need to keep their calling thread responsive while reusing the existing blocking driver APIs. The pool and executor are backend-generic and work with SQLite, native PostgreSQL (libpq), native MySQL/MariaDB, and the ODBC-backed SQL Server, Oracle, and Informix drivers. `ConnectionPool<Backend>::submit()` returns a standard `std::future` and runs the complete callback on a bounded worker executor.

## 1. Basic Usage

```cpp
PoolConfig config;
config.min_connections = 2;
config.max_connections = 8;
config.async_worker_count = 4;
config.async_queue_capacity = 32;

auto pool = make_pool<sqlite>("app.db", config);

{
    auto connection = pool->acquire();
    auto db = connection.get_context();
    db.ensure_table(users_table);
}

auto users_future = pool->submit([](auto& db) {
    return db.from(users_table)
        .where(users_table["active"] == true)
        .order_by(users_table["name"])
        .to_list();
});

auto users = users_future.get();
```

The callback receives a `DbContext<Backend>&` backed by one exclusive pooled connection. It can use the existing synchronous query, materialization, write, batch, and transaction APIs.

## 2. Configuration

`PoolConfig` includes two asynchronous settings:

| Option | Default | Behavior |
|---|---:|---|
| `async_worker_count` | `0` | `0` uses `max_connections`; explicit values are clamped to the connection limit. |
| `async_queue_capacity` | `64` | Maximum number of jobs waiting for a worker. Values below `1` are normalized to `1`. |

Workers are created lazily when the first submission is accepted. Worker count is capped by `max_connections`; each running callback needs an exclusive connection lease. The queue is bounded to prevent an overloaded caller from accumulating unbounded memory.

## 3. Results and Errors

The result type is deduced from invoking the callback with `DbContext<Backend>&`:

```cpp
auto count_future = pool->submit([](auto& db) {
    return db.from(users_table).count();
});

try {
    std::size_t count = count_future.get();
} catch (const DbException& error) {
    // Pool acquisition, driver, and callback errors arrive through get().
}
```

`void` callbacks and move-only result values are supported. Callbacks returning references are rejected at compile time. Return materialized values, counts, IDs, or affected-row totals rather than objects tied to the connection.

## 4. Backpressure

Submission never waits for queue capacity. When the queue is full, `submit()` returns an already-ready exceptional future. Calling `get()` on that future throws `DbException`. A submission made after shutdown behaves the same way with a pool-closed error.

## 5. Queued Cancellation

Pass a `std::stop_token` to request cancellation before a queued callback starts. The token is checked before connection acquisition and again immediately before callback invocation. This is a best-effort race: if a worker has already passed both checks, the callback runs to completion and `get()` returns its normal result.

```cpp
std::stop_source stop_source;
auto future = pool->submit(stop_source.get_token(), [](auto& db) {
    return db.from(users_table).count();
});

stop_source.request_stop();

try {
    future.get();
} catch (const OperationCancelled&) {
    // A cancellation check observed the token before the callback was invoked.
}
```

Cancellation is checked before connection acquisition and immediately before callback invocation, so `OperationCancelled` is produced only when one of those checks observes the token. Cancellation after the final check does not stop a callback that has already started. Existing `ExecutionOptions` and stream-level cancellation can still be used inside a callback when the operation itself needs driver cancellation.

## 6. Lifetime and Transactions

The callback owns the connection lease for its whole duration. A synchronous transaction is valid when it begins, commits, or rolls back inside the same callback:

```cpp
auto future = pool->submit([](auto& db) {
    Transaction transaction(db.connection());
    db.insert(users_table, User{});
    transaction.commit();
});
future.get();
```

Do not return `DbContext`, `QueryBuilder`, `PreparedQuery`, `PreparedCommand`, `RowStream`, `EntityStream`, references, or raw pointers to connection-owned state. The lease is released when the callback returns. Transaction sessions spanning multiple submitted callbacks are not provided by this API.

## 7. Shutdown

`pool->close()` stops accepting submissions, cancels queued jobs with `OperationCancelled`, and closes idle connections. When called from another thread, it joins the workers, so it waits for already-running callbacks to finish. When called from inside a callback on that same pool, the current worker is detached instead of joined; `close()` returns without waiting for that callback. A callback that calls `close()` on its own pool is therefore supported, but it cannot wait for itself to finish. Outstanding futures remain the source of their callback result or shutdown exception.

Do not call `submit()` from a running callback while another thread is closing the same pool. `close()` holds the submission mutex while joining workers, so that sequence can deadlock.

The existing synchronous APIs on `DbContext`, `QueryBuilder`, and `PooledConnection` are unchanged. Native driver asynchronous interfaces and asynchronous streaming are outside this layer; current blocking drivers execute on pool workers.
