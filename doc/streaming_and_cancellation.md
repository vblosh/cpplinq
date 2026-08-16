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
                                  │    (C++20 Input Range)    │
                                  └───────┬───────────┬───────┘
                                          │           │
                     lazy row pull (next) │           │ on stop_token.stop_requested()
                                          │           │
                       ┌──────────────────▼─┐       ┌─▼──────────────────┐
                       │   IDataReader      │       │ statement::cancel()│
                       │ (cursor kept alive)│       │  (SQLCancel /      │
                       └────────────────────┘       │   sqlite3_interrupt│
                                                    │   PQcancel)        │
                                                    └─┬──────────────────┘
                                                      │
                                            ┌─────────▼──────────────┐
                                            │  operation_cancelled   │
                                            │  (or unsupported_feat) │
                                            └────────────────────────┘
```

---

## 2. Streaming with `RowStream` & `EntityStream`

Instead of materializing millions of rows into a `std::vector`, `connection.stream()` and `query_builder.stream()` return move-only, single-pass C++20 input ranges. The prepared statement and cursor reader remain alive while rows are lazily consumed one-by-one.

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
    // Process row immediately without caching entire result set in memory
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
1. The registered `std::stop_callback` triggers underlying driver cancellation (`SQLCancel` on ODBC, `sqlite3_interrupt` on SQLite, or `PQcancel` on PostgreSQL).
2. The stream iterator aborts and throws `cpplinq::operation_cancelled`.

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
}
```

---

## 4. Explicit Statement Cancellation

When application code manages an `IPreparedStatement` directly, calling `.cancel()` halts the running query asynchronously from any thread:

```cpp
auto stmt = conn->prepare("SELECT * FROM huge_table");

// On a worker or timer thread:
stmt->cancel();
```

---

## 5. Driver Capabilities & Info Introspection

Database drivers query metadata once on connection and expose them via `.info()` and `.capabilities()`. This avoids brittle string matching on driver names:

```cpp
const auto& caps = db.connection().capabilities();

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

| Capability | SQLite | PostgreSQL | MSSQL | MySQL / MariaDB |
|---|---|---|---|---|
| `cancel` | ✅ `sqlite3_interrupt` | ✅ `PQcancel` | ✅ `SQLCancel` | ✅ `SQLCancel` |
| `streaming` | ✅ | ✅ | ✅ | ✅ |
| `query_timeout` | ✅ `busy_timeout` | ✅ `statement_timeout` | ✅ `SQL_ATTR_QUERY_TIMEOUT` | ✅ `SQL_ATTR_QUERY_TIMEOUT` |
| `transactions` | ✅ `BEGIN/COMMIT` | ✅ `BEGIN/COMMIT` | ✅ ODBC Tran | ✅ ODBC Tran |
| `savepoints` | ✅ | ✅ | ✅ | ✅ |
| `returning_clause`| ✅ | ✅ | ❌ | ❌ |
| `output_clause` | ❌ | ❌ | ✅ | ❌ |
| `upsert` | ✅ `ON CONFLICT` | ✅ `ON CONFLICT` | ✅ `MERGE` | ✅ `ON DUPLICATE` |
| `window_functions`| ✅ | ✅ | ✅ | ✅ |
| `ctes` | ✅ `WITH` | ✅ `WITH` | ✅ `WITH` | ✅ `WITH` |
