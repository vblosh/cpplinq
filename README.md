# cpplinq — LINQ-to-SQL for Modern C++ (C++20)

`cpplinq` is a type-safe, fluent database query library for C++20 inspired by .NET's LINQ to SQL and Entity Framework Core. It brings compile-time type checking, expression tree generation, struct-based row mapping, and a fluent query API to relational databases.

---

## Features

- **Type-Safe Expression AST**: Operator overloading (`==`, `!=`, `<`, `<=`, `>`, `>=`, `&&`, `||`, `!`) builds a compile-time expression tree rather than string concatenation.
- **Fluent Query Builder**: Method chaining (`.from()`, `.where()`, `.order_by()`, `.limit()`, `.offset()`) with deferred execution.
- **Automatic Struct Mapping**: Maps database records directly into plain C++ structs without external pre-compilers or intrusive macros.
- **Nullable Columns with `std::optional`**: Native SQL `NULL` handling seamlessly mapped to `std::optional<T>`.
- **Database Abstraction**: Pluggable SQL dialect (`ISqlDialect`) and driver (`IConnection`, `IPreparedStatement`, `IDataReader`) architecture.
  - **SQLite** (included out of the box via built-in amalgamation)
  - **PostgreSQL** (via `libpq` with ODBC DSN support)
  - **Microsoft SQL Server** (native Windows ODBC driver with `[brackets]`, `OUTPUT INSERTED`, `OFFSET...FETCH`)
- **Full CRUD & Aggregates**:
  - `insert` & `insert_many` (with `RETURNING` and `OUTPUT INSERTED` ID support)
  - `where(...)` filtering with chained logical operators
  - `order_by` / `order_by_desc`
  - `limit` / `offset` pagination
  - `update` with typed column assignments
  - `remove` (DELETE)
  - `count()`, `avg()`, `min_val()`, `max_val()`, `sum()`
  - `begin_transaction()` with RAII rollback/commit semantics

---

## Quick Start Example

```cpp
#include <iostream>
#include <string>
#include <optional>
#include <cpplinq/cpplinq.hpp>

using namespace cpplinq;

// 1. Define your Entity struct
struct User {
    int id = 0;
    std::string name;
    std::optional<std::string> email;
    int age = 0;
};

// 2. Define the Table Schema
inline const auto users_table = table<User>(
    "users",
    column("id",    &User::id,    primary_key, auto_increment),
    column("name",  &User::name,  not_null),
    column("email", &User::email),
    column("age",   &User::age,   not_null)
);

int main() {
    // 3. Connect to Database:
    // SQLite:
    auto db = cpplinq::connect<cpplinq::sqlite>(":memory:");
    // PostgreSQL:
    // auto db = cpplinq::connect<cpplinq::postgres>("PostgreSQL35W");
    // Microsoft SQL Server:
    // auto db = cpplinq::connect<cpplinq::mssql>("Driver={ODBC Driver 17 for SQL Server};Server=localhost;Database=testdb;Trusted_Connection=yes;");

    // 4. Create table if not exists
    db.ensure_table(users_table);

    // 5. Insert records
    int64_t alice_id = db.insert(users_table, User{0, "Alice", "alice@example.com", 30});
    int64_t bob_id   = db.insert(users_table, User{0, "Bob",   std::nullopt,        25});

    // 6. Query with LINQ-like fluent syntax
    auto users = db.from(users_table)
                   .where(users_table["age"] >= 25 && users_table["name"] != "Bob")
                   .order_by_desc(users_table["age"])
                   .to_vector();

    for (const auto& u : users) {
        std::cout << u.name << " (" << u.age << ")\n";
    }

    // 7. Aggregates
    size_t count = db.from(users_table).count();
    auto avg_age = db.from(users_table).avg(users_table["age"]);
    std::cout << "Count: " << count << ", Avg age: " << (avg_age ? *avg_age : 0.0) << "\n";

    // 8. Update
    db.from(users_table)
      .where(users_table["id"] == bob_id)
      .update({
          users_table["email"] = "bob.new@example.com",
          users_table["age"]   = 26
      });

    // 9. Delete
    db.from(users_table)
      .where(users_table["age"] < 18)
      .remove();

    // 10. RAII Transactions
    {
        auto txn = db.begin_transaction();
        db.insert(users_table, User{0, "Charlie", "charlie@test.com", 22});
        txn.commit(); // If not committed, automatically rolls back on scope exit!
    }

    return 0;
}
```

---

## Building and Running Tests

### Prerequisites
- C++20 compliant compiler (MSVC 2022+, GCC 11+, Clang 13+)
- CMake 3.20+

### Build Instructions

```bash
# Configure (SQLite, PostgreSQL, and MSSQL enabled)
cmake -B build -DCPPLINQ_BUILD_TESTS=ON -DCPPLINQ_BUILD_EXAMPLES=ON -DCPPLINQ_ENABLE_SQLITE=ON -DCPPLINQ_ENABLE_POSTGRES=ON -DCPPLINQ_ENABLE_MSSQL=ON

# Build
cmake --build build --config Release

# Run all tests (PostgreSQL/MSSQL integration tests will run if environment vars are set, or skip cleanly if not set)
ctest --test-dir build --output-on-failure -C Release

# To run PostgreSQL / MSSQL integration tests with custom DSNs or connection strings:
# PowerShell:
$env:CPPLINQ_POSTGRES_ODBC="PostgreSQL35W"
$env:CPPLINQ_MSSQL_ODBC="MSSQLLocalDB"
ctest --test-dir build --output-on-failure -C Release

# Run example application
./build/examples/Release/basic_usage
```

---

## Project Structure

```
cppdb1/
├── include/cpplinq/
│   ├── cpplinq.hpp               # Master header
│   ├── core/
│   │   ├── column.h              # ColumnDef and constraints (primary_key, etc.)
│   │   ├── table.h               # TableDef compile-time schema mapping
│   │   ├── expression.h          # Expression AST & operator overloads
│   │   ├── query_builder.h       # Fluent query builder with deferred execution
│   │   ├── db_context.h          # DbContext top-level facade
│   │   └── sql_generator.h       # AST to parameterized SQL translator
│   ├── mapping/
│   │   ├── row_mapper.h          # Struct materialization engine
│   │   └── type_traits.h         # C++ to SQL type deduction & is_nullable
│   ├── dialect/
│   │   └── dialect.h             # ISqlDialect interface
│   └── driver/
│       └── connection.h          # IConnection, IPreparedStatement, IDataReader (sqlite, postgres, mssql tags)
├── src/
│   ├── sql_generator.cpp         # Dialect-aware SQL generator implementation
│   ├── dialect/
│   │   ├── sqlite_dialect.h/.cpp
│   │   ├── postgres_dialect.h/.cpp
│   │   └── mssql_dialect.h/.cpp
│   └── driver/
│       ├── sqlite_connection.h/.cpp
│       ├── postgres_connection.h/.cpp (with Windows ODBC DSN resolution)
│       └── mssql_connection.h/.cpp    (native Windows ODBC driver)
├── tests/
│   ├── test_expression.cpp          # 34 tests: AST nodes, operators, literals
│   ├── test_query_builder.cpp       # 23 tests: SQL generation & SQLite/PG/MSSQL formats
│   ├── test_row_mapper.cpp          # 6 tests: struct & optional field mapping
│   ├── test_sqlite_integration.cpp  # 11 tests: end-to-end in-memory SQLite CRUD
│   ├── test_postgres_integration.cpp # 12 tests: PostgreSQL CRUD via CPPLINQ_POSTGRES_ODBC
│   └── test_mssql_integration.cpp   # 12 tests: MSSQL CRUD via CPPLINQ_MSSQL_ODBC
└── examples/
    └── basic_usage.cpp              # Complete runnable demonstration
```

---

## License
MIT License.
