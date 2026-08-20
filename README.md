# cpplinq — Type-Safe LINQ-to-SQL for Modern C++ (C++20)

`cpplinq` is an expressive, compile-time type-safe database query and object-relational mapping (ORM) library for C++20 inspired by .NET's LINQ to SQL and Entity Framework Core. It delivers compile-time query syntax checking, operator-overloaded expression AST generation, zero-overhead struct hydration, cross-dialect SQL generation, and connection pooling.

---

## ✨ Features

- 🛡️ **Type-Safe Expression AST**: Operator overloading (`==`, `!=`, `<`, `<=`, `>`, `>=`, `&&`, `||`, `!`) builds a strongly-typed expression tree, preventing syntax and runtime type errors.
- ⚡ **Zero-Overhead Struct Mapping**: Maps database records directly into plain C++ structs and tuples (`std::pair`, `std::tuple`) without macros, code generators, or reflection boilerplate.
- 💎 **Rich Cross-Database Data Types**: Full native support for exact-precision `SqlNumeric` (decimals), dates (`SqlDate`, `std::chrono::year_month_day`), times (`SqlTime`, `std::chrono::hh_mm_ss`), timestamps (`SqlTimestamp`, `std::chrono::system_clock::time_point`), intervals/durations (`SqlInterval`, `std::chrono::duration`), native UUIDs/GUIDs (`SqlGuid`), UTF-16 Unicode wide strings (`std::wstring`), BLOBs (`std::vector<uint8_t>`), and nullables (`std::optional<T>`).
- 🔗 **Multi-Table Joins & Relationships**: Type-safe `INNER JOIN` and `LEFT JOIN` (2 and 3+ tables) with tuple/pair hydration and nullable `std::optional<T>` mapping.
- 🧱 **Common Table Expressions (CTEs)**: Fluent `WITH cte AS (...) SELECT ...` queries via `.with_cte()`.
- 🪟 **Window Functions**: `ROW_NUMBER()`, `RANK()`, `DENSE_RANK()`, and aggregate window functions (`SUM/AVG/COUNT OVER`) with `.over().partition_by(...).order_by(...)`.
- 📅 **Cross-Dialect Date & Time Functions**: `now()`, `current_date()`, `col.year()`, `col.month()`, `col.day()`, and `col.add_days()` mapped cleanly across SQLite, PostgreSQL, MySQL / MariaDB, Microsoft SQL Server, Oracle Database, IBM Informix.
- 🔍 **Subqueries & Predicates**: `EXISTS`, `NOT EXISTS`, `IN (subquery)`, `LIKE`, `BETWEEN`, `IN (list)`, and scalar subqueries.
- 🔄 **Cross-Dialect UPSERT & Bulk Operations**: Atomic single and batch upserts (`insert_many`, `update_many`, `delete_many`, `upsert_many`, `truncate`) with driver-accelerated ODBC array parameter binding.
- 🔀 **SQL Set Operations**: `union_with` (`UNION`), `union_all` (`UNION ALL`), `intersect` (`INTERSECT`), and `except_from` (`EXCEPT`).
- 🏊 **Thread-Safe Connection Pool**: High-performance `ConnectionPool<Backend>` with RAII leasing (`PooledConnection`), acquisition timeouts, and recycling.
- 📊 **Full Aggregations & Grouping**: `count()`, `count_distinct()`, `sum()`, `avg()`, `min_val()`, `max_val()`, `group_by()`, and `having()`.
- 💾 **Multi-Database Support**:
  - **SQLite** (built-in amalgamation, in-memory & file databases)
  - **PostgreSQL** (native ODBC/libpq with parameter binding and `RETURNING`)
  - **MySQL / MariaDB** (native ODBC driver with backtick quoting `` `table` ``, `ON DUPLICATE KEY UPDATE`, `LAST_INSERT_ID()`)
  - **Microsoft SQL Server** (native Windows ODBC driver with `[brackets]`, `OUTPUT INSERTED`, `OFFSET...FETCH`)
  - **Oracle Database** (native ODBC driver with double-quote identifiers `"table"`, ANSI `OFFSET...FETCH`, `MERGE INTO`, `NUMTODSINTERVAL`)
  - **IBM Informix** (native ODBC driver with double-quote identifiers `"table"`, `SERIAL`, Informix `MERGE`, `UNITS DAY`)

---

## 📚 Documentation

Detailed documentation is available in the [`doc/`](doc/) directory:

- [**Architecture & Design**](doc/architecture.md) — Internal design, AST engine, dialect system, driver layer, and connection pooling.
- [**Querying & Filtering Guide**](doc/query_guide.md) — Where filters, boolean logic, ranges (`BETWEEN`), patterns (`LIKE`), lists (`IN`), sorting (`ORDER BY`), and paging (`LIMIT`/`OFFSET`).
- [**Row Materialization & Memory Management**](doc/materialization.md) — `.to_list()`, `ChunkedList` zero-copy in-place hydration, `.first()`, `.count()`, `.stream()`, and compile-time struct hydration.
- [**Joins, Subqueries & CTEs**](doc/joins_and_relationships.md) — 2-table & 3-table joins, tuple mapping, correlated subqueries, `EXISTS`, and Common Table Expressions.
- [**Functions, Aggregates & Window Functions**](doc/functions_and_aggregates.md) — Scalar SQL functions, date/time operations, `GROUP BY`/`HAVING`, and window functions (`ROW_NUMBER`, `RANK`, etc.).
- [**Data Modifications & Transactions**](doc/data_modifications.md) — `insert`, `insert_many`, `update`, `remove`, `upsert`, RAII `Transaction`, and `ConnectionPool`.
- [**Connection Pooling Guide**](doc/connection_pool.md) — Thread-safe `ConnectionPool<Backend>`, RAII `PooledConnection` leasing, timeouts, metrics, and multi-threading.
- [**Streaming & Cancellation Guide**](doc/streaming_and_cancellation.md) — Lazy C++20 range streaming, timeouts, cooperative `std::stop_token` cancellation, `statement::cancel()`, and driver capabilities.
- [**Dialects & Drivers**](doc/dialects_and_drivers.md) — Connecting to SQLite, PostgreSQL, MySQL / MariaDB, Microsoft SQL Server, Oracle Database, IBM Informix, ODBC DSN setup, and CI workflows.
- [**Manual Testing & Building Guide**](doc/manual_testing.md) — Building, testing, sanitizer flags (ASan/UBSan), Docker compose database orchestration, and WSL workflows.

---

## 🚀 Quick Start Example

```cpp
#include <iostream>
#include <string>
#include <optional>
#include <cpplinq/cpplinq.hpp>

using namespace cpplinq;

// 1. Define Entity Structs
struct User {
    int id = 0;
    std::string name;
    std::optional<std::string> email;
    int age = 0;
};

struct Order {
    int id = 0;
    int user_id = 0;
    double amount = 0.0;
};

// 2. Define Table Schemas
inline const auto users_table = table<User>(
    "users",
    column("id",    &User::id,    primary_key, auto_increment),
    column("name",  &User::name,  not_null),
    column("email", &User::email),
    column("age",   &User::age,   not_null)
);

inline const auto orders_table = table<Order>(
    "orders",
    column("id",      &Order::id,      primary_key, auto_increment),
    column("user_id", &Order::user_id, not_null),
    column("amount",  &Order::amount,  not_null)
);

int main() {
    // 3. Connect to Database (SQLite / PostgreSQL / MySQL / MSSQL / Oracle / Informix)
    auto db = cpplinq::connect<cpplinq::sqlite>(":memory:");

    db.ensure_table(users_table);
    db.ensure_table(orders_table);

    // 4. Insert records
    int64_t alice_id = db.insert(users_table, User{0, "Alice", "alice@example.com", 30});
    int64_t bob_id   = db.insert(users_table, User{0, "Bob",   std::nullopt,        25});
    db.insert(orders_table, Order{0, static_cast<int>(alice_id), 199.99});

    // 5. Type-Safe Query with Filters & Sorting (Zero-Copy .to_list())
    auto adults = db.from(users_table)
                    .where(users_table["age"] >= 25 && users_table["email"].is_not_null())
                    .order_by_desc(users_table["age"])
                    .to_list();

    // 6. Multi-Table INNER JOIN (returns ChunkedList<std::pair<User, Order>>)
    auto user_orders = db.from(users_table)
                         .join(orders_table).on(users_table["id"] == orders_table["user_id"])
                         .where(orders_table["amount"] > 100.0)
                         .to_list();

    for (const auto& [user, order] : user_orders) {
        std::cout << user.name << " bought Order #" << order.id << " ($" << order.amount << ")\n";
    }

    // 7. Subquery with EXISTS
    auto buyers = db.from(users_table)
                    .where(exists(
                        db.from(orders_table)
                          .where(orders_table["user_id"] == users_table["id"])
                    ))
                    .to_list();

    // 8. Cross-Dialect UPSERT
    db.upsert(
        users_table,
        User{static_cast<int>(alice_id), "Alice Smith", "alice.smith@example.com", 31},
        {users_table["id"]},
        {users_table["name"], users_table["email"], users_table["age"]}
    );

    // 9. RAII Transaction Guard
    {
        auto txn = db.begin_transaction();
        db.insert(users_table, User{0, "Charlie", "charlie@test.com", 22});
        txn.commit(); // Automatically rolls back if an exception occurs before commit()
    }

    return 0;
}
```

---

## 🛠️ Building & Running Tests

### Prerequisites
- C++20 compliant compiler (MSVC 2022 v17.4+, GCC 11+, Clang 13+)
- CMake 3.20+

### Build
```bash
# Configure with all backends, tests, and examples
cmake -B build -DCPPLINQ_BUILD_TESTS=ON -DCPPLINQ_BUILD_EXAMPLES=ON -DCPPLINQ_ENABLE_SQLITE=ON -DCPPLINQ_ENABLE_POSTGRES=ON -DCPPLINQ_ENABLE_MYSQL=ON -DCPPLINQ_ENABLE_MSSQL=ON -DCPPLINQ_ENABLE_ORACLE=ON -DCPPLINQ_ENABLE_INFORMIX=ON

# Build in Release mode
cmake --build build --config Release --parallel
```

### Run Test Suite
```bash
# Run all test suites
ctest --test-dir build --output-on-failure -C Release

# Optional: Run against live database instances
$env:CPPLINQ_POSTGRES_ODBC="PostgreSQL35W"
$env:CPPLINQ_MYSQL_ODBC="MySQLDSN"
$env:CPPLINQ_MSSQL_ODBC="MSSQLLocalDB"
$env:CPPLINQ_ORACLE_ODBC="OracleDSN"
$env:CPPLINQ_INFORMIX_ODBC="InformixDSN"
ctest --test-dir build --output-on-failure -C Release
```

| Suite Executable | Description |
|---|---|
| `test_expression` | AST construction, boolean operators, date/time AST, window function AST |
| `test_query_builder` | Dialect-aware SQL generator (SELECT, INSERT, CTEs, Joins, Windows, UPSERT, Set Ops) |
| `test_oracle_query_builder` | Oracle dialect-specific query generator and ANSI pagination unit tests |
| `test_informix_query_builder` | IBM Informix dialect-specific query generator unit tests |
| `test_row_mapper` | Struct and tuple row materialization, optional NULL handling |
| `test_chunked_buffer` | Zero-reallocation chunk-based buffer, placement new, and exact-fit vector extraction |
| `test_sqlite_integration` | End-to-end SQLite in-memory integration testing (CRUD, joins, CTEs, subqueries) |
| `test_connection_pool` | Multi-threaded connection leasing, timeouts, recycling (8 threads, 200 ops) |
| `test_streaming` | Single-pass C++20 input ranges, timeouts, and cooperative stop_token cancellation |
| `test_postgres_integration` | Live PostgreSQL integration test suite |
| `test_mysql_integration` | Live MySQL / MariaDB integration test suite |
| `test_mssql_integration` | Live Microsoft SQL Server integration test suite |
| `test_oracle_integration` | Live Oracle Database integration test suite |
| `test_informix_integration` | Live IBM Informix integration test suite |

---

## 📄 License
This project is licensed under the [MIT License](LICENSE).
