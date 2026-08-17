# Row Materialization & Memory Management Guide

`cpplinq` uses compile-time metaprogramming and member pointer reflection to materialize database rows into strongly-typed C++ structs, pairs, and tuples with **true zero-copy in-place hydration, zero runtime reflection overhead, zero intermediate string parsing, and zero redundant memory reallocations**.

---

## 1. Overview of Materialization

All queries in `cpplinq` are constructed using **deferred execution**. Query building, filtering, joining, and sorting merely construct an Abstract Syntax Tree (AST). Database execution and row materialization take place **only** when a terminal method is invoked:

```
                  ┌───────────────────────────────┐
                  │    QueryBuilder AST & SQL     │
                  └──────────────┬────────────────┘
                                 │
                         execute_query()
                                 │
                                 ▼
                  ┌───────────────────────────────┐
                  │   IDataReader Driver Cursor   │
                  └──────────────┬────────────────┘
                                 │
                  ┌──────────────▼────────────────┐
                  │   RowMapper<Entity, Cols...>  │
                  └──────────────┬────────────────┘
                                 │
        ┌────────────────────────┼────────────────────────┐
        ▼                        ▼                        ▼
    .to_list()                .first()                .stream()
(ChunkedList Zero-Copy)   (Single std::optional)   (Lazy C++20 Range)
```

---

## 2. Materialization Methods

### 2.1 `.to_list()` — Zero-Copy In-Place Row Materialization

Executes the query and materializes database rows directly into a **`cpplinq::ChunkedList<Entity, 64>`**. 

```cpp
auto users = db.from(users_table)
               .where(users_table["age"] >= 18)
               .order_by(users_table["name"])
               .to_list();
```

#### Pure Zero-Copy & In-Place Hydration Architecture:
Standard `std::vector::push_back` causes $O(\log N)$ memory reallocations during cursor iteration, repeatedly moving and copying all previously fetched structs. Furthermore, copying or moving structs out of cursor readers into temporary objects adds unnecessary overhead.

`cpplinq` achieves **true zero-copy materialization**:
1. **In-Place Chunk Construction**: As the cursor advances, `list.emplace_back()` constructs the destination entity directly in fixed-size 64-element heap chunk blocks.
2. **Direct Field Writing**: `RowMapper::map_row(*reader, entity_ref)` writes column data directly into the newly constructed memory slot inside the chunk. **Zero temporary entity copies, zero struct moves.**
3. **$O(1)$ Container Return**: When returning from `.to_list()`, `ChunkedList` is transferred by value in $O(1)$ by swapping the chunk pointer table. The entities inside the chunks never move.

```cpp
// Direct usage of ChunkedList as a rich container:
for (const auto& user : users) {
    std::cout << user.name << "\n";
}

// Full Random-Access and Indexing:
User& first_user = users[0];
User& checked_user = users.at(1);

// Standard Algorithms and C++20 Ranges Views:
std::sort(users.begin(), users.end(), [](const auto& a, const auto& b) {
    return a.age < b.age;
});

// Conversion to contiguous std::vector when required:
std::vector<User> vec = users.to_vector();
```

---

### 2.2 `.first()` — Single-Element Materialization

Executes the query with an automatic `LIMIT 1` constraint and materializes at most a single matching entity into a `std::optional<Entity>`:

```cpp
std::optional<User> user = db.from(users_table)
                             .where(users_table["email"] == "alice@example.com")
                             .first();

if (user.has_value()) {
    std::cout << "Found user: " << user->name << " (ID: " << user->id << ")\n";
} else {
    std::cout << "User not found\n";
}
```

- **Optimized SQL**: Automatically appends `LIMIT 1` (or `TOP 1` / `FETCH FIRST 1 ROWS ONLY` depending on the dialect).
- **Zero Heap Waste**: Materializes only 1 entity directly without allocating collection buffers.

---

### 2.3 `.count()` — Scalar Count Materialization

Executes the query and returns the number of matching records as a `size_t`:

```cpp
size_t active_user_count = db.from(users_table)
                             .where(users_table["age"] >= 21)
                             .count();
```

---

### 2.4 `.stream(ExecutionOptions options = {})` — Lazy C++20 Input Range

For large datasets, ETL pipelines, or memory-constrained applications, `.stream()` provides **zero-materialization streaming**. It returns a move-only, single-pass C++20 input range that yields entities one at a time as the cursor advances:

```cpp
cpplinq::ExecutionOptions options;
options.query_timeout_seconds = 30;

for (const User& user : db.from(users_table)
                           .where(users_table["age"] >= 21)
                           .stream(options)) {
    // Process one user at a time - 0 collection heap allocations!
    std::cout << "User: " << user.name << ", Age: " << user.age << "\n";
}
```

#### Key Capabilities:
- **Zero In-Memory Buffering**: Rows are fetched from the database network socket / cursor on demand during range iteration (`++it`).
- **Cooperative Cancellation**: Pass a `std::stop_token` in `ExecutionOptions` to abort large streams cooperatively without blocking.
- **Ranges Interoperability**: Compatible with standard algorithms and views (`std::views::take`, `std::views::filter`, `std::views::transform`).

---

## 3. Multi-Table Join & Outer Join Materialization

When joining tables, `cpplinq` inspects join types at compile time and materializes appropriate compound types directly inside `ChunkedList`:

### Inner Joins $\to$ `ChunkedList<std::pair<E1, E2>>` / `ChunkedList<std::tuple<E1, E2, E3>>`
```cpp
// 2 Tables Inner Join
cpplinq::ChunkedList<std::pair<User, Order>> pairs = 
    db.from(users_table)
      .join(orders_table).on(users_table["id"] == orders_table["user_id"])
      .to_list();

// 3 Tables Inner Join
cpplinq::ChunkedList<std::tuple<User, Order, Account>> triplets = 
    db.from(users_table)
      .join(orders_table).on(users_table["id"] == orders_table["user_id"])
      .join(accounts_table).on(users_table["name"] == accounts_table["username"])
      .to_list();
```

### Left Outer Joins $\to$ `ChunkedList<std::pair<E1, std::optional<E2>>>`
For outer joins where the right-hand entity may not match, `cpplinq` wraps the joined entity in `std::optional`:

```cpp
auto results = db.from(users_table)
                 .left_join(orders_table).on(users_table["id"] == orders_table["user_id"])
                 .to_list();

for (const auto& [user, order_opt] : results) {
    if (order_opt.has_value()) {
        std::cout << user.name << " bought order #" << order_opt->id << "\n";
    } else {
        std::cout << user.name << " has no orders.\n";
    }
}
```

---

## 4. Compile-Time `RowMapper` Architecture

The core mapping engine is `RowMapper<Entity, ColumnDefs...>`. It uses compile-time member pointers and `std::index_sequence` to assign column values directly into struct members without dynamic dispatch:

```cpp
template <size_t I>
void map_single_column(Entity& entity, IDataReader& reader) const {
    const auto& col = std::get<I>(columns_);
    using FieldType = std::remove_cvref_t<decltype(entity.*(col.member_ptr))>;
    int col_idx = static_cast<int>(I) + offset_;

    if (reader.is_null(col_idx)) {
        if constexpr (is_nullable_v<FieldType>) {
            entity.*(col.member_ptr) = std::nullopt;
        }
        return;
    }

    if constexpr (is_nullable_v<FieldType>) {
        using InnerType = typename FieldType::value_type;
        if constexpr (std::is_same_v<InnerType, int>) {
            entity.*(col.member_ptr) = static_cast<int>(reader.get_int64(col_idx));
        } else if constexpr (std::is_same_v<InnerType, std::string>) {
            entity.*(col.member_ptr) = reader.get_string(col_idx);
        }
        // ...
    } else {
        if constexpr (std::is_same_v<FieldType, int>) {
            entity.*(col.member_ptr) = static_cast<int>(reader.get_int64(col_idx));
        } else if constexpr (std::is_same_v<FieldType, std::string>) {
            entity.*(col.member_ptr) = reader.get_string(col_idx);
        }
        // ...
    }
}
```

### Supported Data Types & Conversions:
| C++ Type | SQL / ODBC Mapping | Description / Cross-Dialect Handling |
|---|---|---|
| `int`, `int32_t`, `int64_t`, `int16_t`, `int8_t` | `INTEGER`, `BIGINT`, `SMALLINT`, `TINYINT` | Signed integral types |
| `uint32_t`, `uint64_t`, `uint16_t`, `uint8_t` | `INTEGER`, `BIGINT UNSIGNED`, `NUMERIC(20,0)` | Unsigned integrals with transparent fallback across databases |
| `double`, `float` | `DOUBLE PRECISION`, `FLOAT`, `REAL` | IEEE-754 floating-point numbers |
| `bool` | `BOOLEAN`, `BIT`, `INTEGER` | Truth values (native boolean / bit / int flags) |
| `std::string` | `VARCHAR`, `TEXT`, `NVARCHAR` | UTF-8 string data |
| `std::wstring` | `NVARCHAR`, `WCHAR`, `WVARCHAR` | UTF-16 Unicode wide string data |
| `std::vector<uint8_t>`, `std::vector<char>`, `std::vector<std::byte>` | `BLOB`, `BYTEA`, `VARBINARY(MAX)` | Raw binary byte storage |
| `cpplinq::SqlNumeric` | `NUMERIC(p, s)`, `DECIMAL(p, s)` | Exact-precision fixed-point decimal arithmetic |
| `cpplinq::SqlDate`, `std::chrono::year_month_day` | `DATE` | Gregorian calendar date (`YYYY-MM-DD`) |
| `cpplinq::SqlTime`, `std::chrono::hh_mm_ss` | `TIME` | Wall-clock time (`HH:MM:SS`) |
| `cpplinq::SqlTimestamp`, `std::chrono::system_clock::time_point` | `TIMESTAMP`, `DATETIME`, `DATETIME2` | Date and time instant (`YYYY-MM-DD HH:MM:SS.ffffff`) |
| `cpplinq::SqlInterval`, `std::chrono::duration` | `INTERVAL`, `VARCHAR` | Time interval / duration (`D HH:MM:SS`) |
| `cpplinq::SqlGuid` | `UUID`, `UNIQUEIDENTIFIER`, `VARCHAR(36)` | Standard 128-bit RFC 4122 UUID / GUID |
| `std::optional<T>` | `NULL` | Nullable representation for any of the above types |

---

## 5. Comparison Matrix of Materialization Methods

| Method | Return Type | Memory Footprint | Move/Copy Overhead | Best Use Case |
|---|---|---|---|---|
| **`.to_list()`** | `ChunkedList<Entity, 64>` | Block-allocated chunks | **0 moves, 0 copies** (in-place hydration) | Primary querying, sorting, ranges algorithms, high throughput |
| **`list.to_vector()`** | `std::vector<Entity>` | Contiguous array | 1 sequential move pass | Legacy APIs or C-libraries requiring `data()` contiguous pointers |
| **`.first()`** | `std::optional<Entity>` | $O(1)$ single struct | **0** | Finding by primary key, unique constraint, or existence check |
| **`.count()`** | `size_t` | $O(1)$ scalar | **0** | Pagination counters, cardinality checks |
| **`.stream()`** | `EntityStream` (C++20 Range) | $O(1)$ active row buffer | **0** (no container allocated) | Large datasets, continuous data processing, pipeline transforms |
