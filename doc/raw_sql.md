# Raw SQL Queries & Direct Execution Guide

While `cpplinq` provides a strongly-typed LINQ query builder and expression AST engine, full support for raw SQL queries, prepared statements, cursor readers, and batch execution is built into the core driver architecture.

This guide details how to execute raw SQL queries across all supported backends (SQLite, PostgreSQL, MySQL / MariaDB, Microsoft SQL Server, Oracle Database, and IBM Informix).

---

## 1. Connection Access

You can execute raw SQL directly through an [`IConnection`](../include/cpplinq/driver/connection.h) instance obtained from [`make_connection<Backend>`](../include/cpplinq/driver/connection.h) or via [`DbContext::connection()`](../include/cpplinq/core/db_context.h).

```cpp
#include <cpplinq/cpplinq.hpp>
#include <iostream>

using namespace cpplinq;

// Option A: Standalone Connection
auto conn = make_connection<sqlite>("app.db");
conn->open();

// Option B: From an existing DbContext
auto db = connect<sqlite>("app.db");
IConnection& conn_ref = db.connection();
```

---

## 2. Parameterless Direct SQL Execution

For commands, DDL migrations, or static queries without runtime parameters, direct execution provides a fast execution path.

### 2.1 Direct DDL & Commands (`execute` / `execute_raw`)
Executes an arbitrary SQL statement without returning data or affected row counts.

```cpp
// Direct execution on IConnection
conn->execute("CREATE TABLE IF NOT EXISTS users (id INT PRIMARY KEY, name TEXT, age INT)");

// Or via DbContext convenience method
db.execute_raw("CREATE INDEX IF NOT EXISTS idx_users_age ON users(age)");
```

### 2.2 Direct Non-Query with Affected Row Count (`execute_non_query_direct`)
Executes an `INSERT`, `UPDATE`, or `DELETE` statement and returns the number of modified rows.

```cpp
size_t affected = conn->execute_non_query_direct("DELETE FROM users WHERE age < 18");
std::cout << "Removed " << affected << " inactive users.\n";
```

### 2.3 Direct Query Cursor (`execute_query_direct`)
Executes a `SELECT` statement and returns an [`IDataReader`](../include/cpplinq/driver/connection.h) cursor.

```cpp
std::unique_ptr<IDataReader> reader = conn->execute_query_direct("SELECT id, name, age FROM users");

while (reader->next()) {
    int64_t id       = reader->get_int64(0);
    std::string name = reader->get_string(1);
    bool age_is_null = reader->is_null(2);
    int64_t age      = age_is_null ? 0 : reader->get_int64(2);

    std::cout << id << ": " << name << " (" << age << ")\n";
}
```

---

## 3. Parameterized Prepared Statements

To prevent SQL injection vulnerabilities and allow statement reuse, use prepared statements via `conn->prepare()`. Parameter indices are **0-indexed positional integers** (`?` placeholders).

### 3.1 Parameterized `INSERT` / `UPDATE` / `DELETE`

```cpp
auto stmt = conn->prepare("INSERT INTO users (id, name, age) VALUES (?, ?, ?)");

stmt->bind(0, int64_t(1));
stmt->bind(1, std::string("Alice"));
stmt->bind(2, int64_t(30));

size_t affected = stmt->execute_non_query();
```

#### Reusing Prepared Statements
Call `stmt->reset()` before binding new parameters to reuse the underlying compiled statement:

```cpp
auto stmt = conn->prepare("INSERT INTO logs (event, timestamp) VALUES (?, ?)");

for (const auto& log : log_entries) {
    stmt->bind(0, log.event);
    stmt->bind(1, log.timestamp);
    stmt->execute_non_query();
    stmt->reset();
}
```

### 3.2 Parameterized `SELECT` Query

```cpp
auto stmt = conn->prepare("SELECT id, name, age FROM users WHERE age >= ? AND name LIKE ?");
stmt->bind(0, int64_t(21));
stmt->bind(1, std::string("A%"));

std::unique_ptr<IDataReader> reader = stmt->execute_query();

while (reader->next()) {
    int64_t id       = reader->get_int64(0);
    std::string name = reader->get_string(1);
    int64_t age      = reader->get_int64(2);

    std::cout << id << " | " << name << " | " << age << "\n";
}
```

---

## 4. Supported Data Types for Binding & Reading

`cpplinq` supports rich C++20 and SQL data types across statement parameter bindings and reader column extractions:

| C++ Type | Binding Method | Reader Method | Notes |
|---|---|---|---|
| `int64_t` | `stmt->bind(col, int64_t(val))` | `reader->get_int64(col)` | 64-bit integer |
| `uint64_t` | `stmt->bind(col, uint64_t(val))` | `reader->get_uint64(col)` | 64-bit unsigned integer |
| `double` / `float` | `stmt->bind(col, double(val))` | `reader->get_double(col)` | IEEE floating point |
| `std::string` | `stmt->bind(col, std::string(val))` | `reader->get_string(col)` | UTF-8 encoded string |
| `std::string_view` | `stmt->bind(col, std::string(val))` | `reader->get_string_view(col)` | String view |
| `std::wstring` | `stmt->bind(col, std::wstring(val))` | `reader->get_wstring(col)` | UTF-16 Unicode string |
| `bool` | `stmt->bind(col, bool(val))` | `reader->get_bool(col)` | Boolean flag |
| `std::vector<uint8_t>` | `stmt->bind(col, blob_bytes)` | `reader->get_blob(col)` | Binary large object (BLOB) |
| `SqlNumeric` | `stmt->bind(col, SqlNumeric("123.45"))` | `reader->get_numeric(col)` | Exact-precision decimal |
| `SqlDate` | `stmt->bind(col, SqlDate(2026, 8, 21))` | `reader->get_date(col)` | Calendar date (`YYYY-MM-DD`) |
| `SqlTime` | `stmt->bind(col, SqlTime(14, 30, 0))` | `reader->get_time(col)` | Time of day (`HH:MM:SS`) |
| `SqlTimestamp` | `stmt->bind(col, SqlTimestamp(...))` | `reader->get_timestamp(col)` | Timestamp with optional fractional precision |
| `SqlGuid` | `stmt->bind(col, SqlGuid("..."))` | `reader->get_guid(col)` | Native 128-bit UUID/GUID |
| `SqlInterval` | `stmt->bind(col, interval)` | `reader->get_interval(col)` | Day/time duration interval |
| `NULL` | `stmt->bind(col, std::monostate{})` | `reader->is_null(col)` | SQL `NULL` literal |

---

## 5. Lazy C++20 Range Streaming (`conn->stream`)

For large result sets, [`conn->stream()`](../include/cpplinq/driver/connection.h) returns a single-pass C++20 input range ([`RowStream`](../include/cpplinq/core/streaming.h)) over [`RowRecord`](../include/cpplinq/core/streaming.h):

```cpp
#include <cpplinq/core/streaming.h>

// Iterate lazily over raw query results
for (const RowRecord& row : conn->stream("SELECT id, name, age FROM users WHERE age > ?", {int64_t(18)})) {
    int64_t id       = row.get_int64(0);
    std::string name = row.get_string(1);
    int64_t age      = row.get_int64(2);

    std::cout << id << " | " << name << " | " << age << "\n";
}
```

### `RowRecord` Helper API
[`RowRecord`](../include/cpplinq/core/streaming.h) provides safe column conversions:
- `row.column_count()` — Returns the number of columns in the row.
- `row.is_null(col)` — Checks if the column value is `NULL`.
- `row.get_int64(col)` / `row.get_double(col)` / `row.get_string(col)` / `row.get_bool(col)` / `row.get_blob(col)` — Safe type casting with fallback defaults.

---

## 6. Execution Options, Timeouts & Cooperative Cancellation

You can pass an [`ExecutionOptions`](../include/cpplinq/driver/connection.h) struct to configure statement timeouts and `std::stop_token` cooperative cancellation:

```cpp
#include <stop_token>

std::stop_source stop_source;

ExecutionOptions options;
options.query_timeout_seconds = 10;
options.stop_token = stop_source.get_token();

try {
    for (const RowRecord& row : conn->stream("SELECT * FROM heavy_table", {}, options)) {
        if (/* condition to abort */) {
            stop_source.request_stop(); // Issues statement cancellation
        }
    }
} catch (const OperationCancelled& ex) {
    std::cerr << "Query cancelled gracefully: " << ex.what() << "\n";
} catch (const DbException& ex) {
    std::cerr << "Database error occurred: " << ex.what() << "\n";
}
```

---

## 7. Fast Batch Array Parameter Inserts

When inserting large volumes of rows with raw SQL, [`conn->insert_many_batch()`](../include/cpplinq/driver/connection.h) leverages backend driver array parameter binding when available:

```cpp
// Prepare flat parameter buffer for 1,000 rows (2 columns: id, name)
std::vector<BoundValue> flat_params;
flat_params.reserve(2000);

for (int64_t i = 1; i <= 1000; ++i) {
    flat_params.emplace_back(i);
    flat_params.emplace_back("User_" + std::to_string(i));
}

size_t inserted = conn->insert_many_batch(
    "INSERT INTO users (id, name) VALUES (?, ?)",
    flat_params,
    /* col_count = */ 2,
    /* row_count = */ 1000
);
```

---

## 8. RAII Transactions with Raw SQL

The [`Transaction`](../include/cpplinq/driver/connection.h) guard ensures ACID transaction guarantees. If an uncaught exception is thrown or `tx.commit()` is not reached, the transaction automatically rolls back upon destruction:

```cpp
{
    Transaction tx(*conn);

    auto stmt = conn->prepare("UPDATE accounts SET balance = balance - ? WHERE id = ?");
    stmt->bind(0, double(250.0));
    stmt->bind(1, int64_t(1));
    stmt->execute_non_query();

    stmt->reset();
    stmt->bind(0, double(-250.0));
    stmt->bind(1, int64_t(2));
    stmt->execute_non_query();

    tx.commit(); // Mark transaction as successful
} // If commit() was not called, rollback occurs automatically here
```
