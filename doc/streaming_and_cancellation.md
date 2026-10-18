# Streaming, Cooperative Cancellation & Driver Capabilities

This document details the streaming range engine, execution options with timeouts, cooperative cancellation via `std::stop_token`, explicit statement cancellation, and driver introspection in **`cpplinq`**.

---

## 1. Overview & Architecture

```
                                  ┌───────────────────────────┐
                                  │     ExecutionOptions      │
                                  │  - query_timeout_seconds  │
                                  │  - std::stop_token        │
                                  └─────────────┬─────────────┘
                                                │
                                  ┌─────────────▼─────────────┐
                                  │    connection::stream     │
                                  │   query_builder::stream   │
                                  └─────────────┬─────────────┘
                                                │
                                  ┌─────────────▼─────────────┐
                                  │   RowStream / EntityStream│
                                  │   (single-pass range)     │
                                  └───────┬───────────┬───────┘
                                          │           │
                     lazy row pull (next) │           │ on stop_token.stop_requested()
                                          │           │
                       ┌──────────────────▼─┐       ┌─▼──────────────────┐
                       │   IDataReader      │       │ statement::cancel()│
                       │ (cursor or buffered│       │  (SQLCancel /      │
                       │  driver result)    │       │   sqlite3_interrupt│
                       └────────────────────┘       │   PQcancel /       │
                                                    │   mysql_kill)      │
                                                    └─┬──────────────────┘
                                                      │
                                            ┌─────────▼──────────────┐
                                            │   driver-specific      │
                                            │   cancellation outcome │
                                            └────────────────────────┘
```

---

## 2. Streaming with `RowStream` & `EntityStream`

`connection.stream()` and `query_builder.stream()` return move-only, single-pass ranges that support range-based `for`. They do not model `std::ranges::input_range`. Rows are exposed one at a time, but actual result buffering depends on the driver.

SQLite and the ODBC backends (MSSQL, Oracle, and Informix) fetch rows from a cursor as iteration advances. PostgreSQL uses `PQexecPrepared`, which returns a complete `PGresult`, and MySQL calls `mysql_stmt_store_result()`. Those two drivers therefore buffer the full result set in the client before row iteration begins, so streaming does not reduce peak memory for them.

### 2.1 Raw SQL Streaming

```cpp
#include <cpplinq/cpplinq.hpp>

using namespace cpplinq;

execution_options options;
options.query_timeout_seconds = 30;

for (const auto& row : db.connection().stream(
         "SELECT id, name, balance FROM users ORDER BY id", {}, options)) {
    int64_t id = row.get_int64(0);
    std::string name = row.get_string(1);
    double balance = row.get_double(2);
    // Process each row as it is exposed by the range.
}
```

### 2.2 Strongly-Typed LINQ Entity Streaming

```cpp
for (const auto& user : db.from(users_table)
                          .where(users_table["age"] >= 18)
                          .order_by(users_table["name"])
                          .stream(options)) {
    std::cout << "User: " << user.name << " (" << user.age << ")\n";
}
```

---

## 3. Cooperative Cancellation via `std::stop_token`

Pass a `std::stop_token` (from `std::stop_source`) inside `execution_options` for cooperative cancellation.

When cancellation is requested:

1. The registered callback calls `IPreparedStatement::cancel()`. The built-in drivers use `sqlite3_interrupt` on SQLite, `PQcancel` on PostgreSQL, `SQLCancel` on ODBC backends, and `mysql_kill` on MySQL.
2. At the start of each iterator advance, the stream checks the token and throws `cpplinq::operation_cancelled` when it is already set.

`cpplinq::operation_cancelled` is guaranteed only for that pre-`next()` token check. If cancellation happens while the iterator is blocked inside `IDataReader::next()`, the driver cancellation may surface as a driver `DbException`, may make the stream end normally, or may be observed on the following advance. The implementation does not guarantee the exception type for a blocking driver call.

```cpp
#include <stop_token>
#include <thread>

std::stop_source stop_source;
execution_options options;
options.stop_token = stop_source.get_token();

try {
    for (const auto& row : db.from(large_table).stream(options)) {
        // Process row...
        if (some_condition) {
            stop_source.request_stop(); // Cooperative cancel
        }
    }
} catch (const cpplinq::operation_cancelled& ex) {
    std::cout << "Query safely cancelled: " << ex.what() << "\n";
} catch (const cpplinq::DbException& ex) {
    std::cout << "Query stopped by the driver: " << ex.what() << "\n";
}
```

---

## 4. Explicit Statement Cancellation

When application code manages an `IPreparedStatement` directly, `cancel()` attempts to halt the running query. Cancellation support and thread safety are driver-specific:

- SQLite uses `sqlite3_interrupt`, which is designed for cross-thread interruption.
- PostgreSQL uses a separate `PQcancel` handle from `PQgetCancel`.
- ODBC uses `SQLCancel`; whether it interrupts a running statement and whether the driver permits it from another thread depends on the ODBC driver.
- MySQL calls `mysql_kill(conn_, mysql_thread_id(conn_))` on the same `MYSQL*` connection handle used by the running statement. Do not assume that calling it concurrently from another thread is safe.

```cpp
auto stmt = conn->prepare("SELECT * FROM huge_table");

// Driver-dependent cancellation. Use a worker/timer thread only when the
// driver supports concurrent cancellation of this connection.
stmt->cancel();
```

---

## 5. Driver Capabilities & Info Introspection

`DriverCapabilities` and `DriverInfo` are exposed through `IConnection::capabilities()` and `IConnection::info()`. They are not cached: `capabilities()` constructs a new structure on each call, and `info()` queries driver or server metadata on each call. ODBC implementations call `SQLGetInfo`, while native drivers query their client library.

```cpp
const auto caps = db.connection().capabilities();

if (!caps.cancel) {
    // Use an application-level stop policy for drivers without native cancellation
}

if (caps.returning_clause) {
    // Driver supports INSERT ... RETURNING
}

auto info = db.connection().info();
std::cout << "Connected to: " << info.dbms_name
          << " v" << info.dbms_version
          << " via " << info.driver_name << "\n";
```

### Capability Matrix

| Capability | SQLite | PostgreSQL | MSSQL | MySQL / MariaDB | Oracle | Informix |
|---|---|---|---|---|---|---|
| `cancel` | ✅ `sqlite3_interrupt` | ✅ `PQcancel` | ✅ `SQLCancel` | ✅ `mysql_kill` | ✅ `SQLCancel` | ✅ `SQLCancel` |
| `streaming` | ✅ cursor (`sqlite3_step`) | ⚠️ full `PGresult` | ✅ cursor (ODBC fetch) | ⚠️ stored result | ✅ cursor (ODBC fetch) | ✅ cursor (ODBC fetch) |
| `query_timeout` | ✅ `busy_timeout` (lock waits only) | ✅ `statement_timeout` | ✅ `SQL_ATTR_QUERY_TIMEOUT` | ✅ `SET max_execution_time` | ✅ `SQL_ATTR_QUERY_TIMEOUT` | ✅ `SQL_ATTR_QUERY_TIMEOUT` |
| `transactions` | ✅ `BEGIN`/`COMMIT` | ✅ `BEGIN`/`COMMIT` | ✅ ODBC autocommit and `SQLEndTran` | ✅ `START TRANSACTION` | ✅ ODBC autocommit and `SQLEndTran` | ✅ ODBC autocommit and `SQLEndTran` |
| `savepoints` | ❌ no API | ❌ no API | ❌ no API | ❌ no API | ❌ no API | ❌ no API |
| `returning_clause` | ✅ | ✅ | ❌ (`OUTPUT` instead) | ❌ | ❌ | ❌ |
| `output_clause` | ❌ | ❌ | ✅ | ❌ | ❌ | ❌ |
| `upsert` | ✅ `ON CONFLICT` | ✅ `ON CONFLICT` | ✅ `MERGE` | ✅ `ON DUPLICATE` | ✅ `MERGE` | ✅ `MERGE` |
| `window_functions` | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| `ctes` | ✅ `WITH` | ✅ `WITH` | ✅ `WITH` | ✅ `WITH` | ✅ `WITH` | ✅ `WITH` |

`DriverCapabilities::savepoints` is currently reported as `true` by the built-in drivers, but `IConnection` and `Transaction` expose no savepoint operations and no code consumes the flag. Treat savepoints as unsupported until an API is added.
