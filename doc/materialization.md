# Row Materialization & Memory Management Guide

`cpplinq` uses compile-time metaprogramming and member pointer reflection to materialize database rows into strongly-typed C++ structs, pairs, and tuples. During list materialization, entities are constructed in place inside chunk storage without temporary entity copies or moves; scalar fields are written directly, while string and blob fields are copied into owned values. `.first()` materializes one entity in a chunk and moves it into the returned optional. The mapper performs no runtime reflection and does not reallocate previously materialized chunks.

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
(ChunkedList In-Place)    (Single std::optional)   (Lazy Single-Pass Range)
```

---

## 2. Materialization Methods

### 2.1 `.to_list()` — In-Place Row Materialization

Executes the query and materializes database rows directly into a **`cpplinq::ChunkedList<Entity, 64>`**.

```cpp
auto users = db.from(users_table)
               .where(users_table["age"] >= 18)
               .order_by(users_table["name"])
               .to_list();
```

#### In-Place Hydration Architecture:

Standard `std::vector::push_back` causes $O(\log N)$ memory reallocations during cursor iteration, repeatedly moving and copying all previously fetched structs. Furthermore, copying or moving structs out of cursor readers into temporary objects adds unnecessary overhead.

`cpplinq` materializes in place without constructing temporary entities:

1. **In-Place Chunk Construction**: As the cursor advances, `list.emplace_back()` constructs the destination entity directly in fixed-size 64-element heap chunk blocks.
2. **Direct Field Writing**: `RowMapper::map_row(*reader, entity_ref)` writes column data directly into the newly constructed memory slot inside the chunk. **No temporary entity copies or struct moves; string and blob fields are copied from the driver into their owned values.**
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

// Conversion to contiguous std::vector. This lvalue overload copies each
// element; use std::move(users).to_vector() to move them instead.
std::vector<User> vec = users.to_vector();
```

---

### 2.2 `.first()` — Single-Element Materialization

Executes the query with an automatic row limit of 1 and materializes at most a single matching entity into a `std::optional<Entity>`:

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

- **Optimized SQL**: Automatically applies a limit of 1 using the dialect's syntax: `LIMIT 1` on SQLite, PostgreSQL, and MySQL; `OFFSET 0 ROWS FETCH NEXT 1 ROWS ONLY` on SQL Server and Oracle; or `FIRST 1` on Informix.
- **One Chunk, One Entity**: `first()` calls `to_list()` with the limit set to 1, so it allocates one 64-element chunk and moves the single entity out into the returned `std::optional`. It does not allocate a result-sized collection.

---

### 2.3 `.count()` — Scalar Count Materialization

Executes `SELECT COUNT(*) FROM <table> [WHERE ...]` and returns the number of matching rows as a `size_t`. It does not include `DISTINCT`, `GROUP BY`, `HAVING`, CTEs, `ORDER BY`, `LIMIT`, or `OFFSET` from the builder; use `count_distinct(users_table["column"])` for a distinct count. In particular, `group_by(...).count()` counts ungrouped matching rows, and `limit(...).count()` still counts all matching rows.

```cpp
size_t active_user_count = db.from(users_table)
                             .where(users_table["age"] >= 21)
                             .count();
```

---

### 2.4 `.stream(ExecutionOptions options = {})` — Lazy Single-Pass Row Range

For large datasets, ETL pipelines, or memory-constrained applications, `.stream()` provides row-by-row streaming without materializing a result collection. It returns a move-only, single-pass range that yields entities one at a time as rows are read from the driver. It supports range-based `for` loops, but it does not model `std::ranges::input_range`, so `std::views` adaptors are not supported:

```cpp
cpplinq::ExecutionOptions options;
options.query_timeout_seconds = 30;

for (const User& user : db.from(users_table)
                           .where(users_table["age"] >= 21)
                           .stream(options)) {
    // Process one user at a time; no result collection is allocated.
    std::cout << "User: " << user.name << ", Age: " << user.age << "\n";
}
```

#### Key Capabilities:

- **No Result Collection**: No `ChunkedList` or `std::vector` is built. SQLite and ODBC cursors deliver rows on demand; the native PostgreSQL and MySQL drivers currently buffer the complete result set before iteration begins.
- **Cooperative Cancellation**: Pass a `std::stop_token` in `ExecutionOptions` to request driver cancellation. The iterator checks the token before each advance; whether cancellation interrupts an in-flight driver fetch is driver-dependent.
- **Range-Based Iteration**: Works with range-based `for` loops. The iterator is move-only and the sentinel does not satisfy the `std::ranges` sentinel requirements, so the stream is not a `std::ranges::input_range` and cannot be piped through `std::views::take`, `std::views::filter`, or `std::views::transform`.

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

For outer joins, `cpplinq` wraps the joined entity in `std::optional`. The value is `std::nullopt` when every projected column of the joined entity is SQL `NULL`. A matched row whose selected columns are all `NULL` therefore also maps to `std::nullopt`; the mapper does not emit a separate match flag:

```cpp
auto results = db.from(users_table)
                 .left_join(orders_table).on(users_table["id"] == orders_table["user_id"])
                 .to_list();

for (const auto& [user, order_opt] : results) {
    if (order_opt.has_value()) {
        std::cout << user.name << " bought order #" << order_opt->id << "\n";
    } else {
        std::cout << user.name << " has no non-NULL order fields.\n";
    }
}
```

---

## 4. Compile-Time `RowMapper` Architecture

The core mapping engine is `RowMapper<Entity, ColumnDefs...>`. It uses compile-time member pointers and `std::index_sequence` to assign column values directly into struct members. The reader interface is still virtual and string/blob fields allocate storage, but the mapper itself performs no per-row type lookup:

```cpp
template <size_t I>
void map_single_column(Entity& entity, IDataReader& reader) const {
    const auto& col = std::get<I>(columns_);
    using FieldType = std::remove_cvref_t<decltype(entity.*(col.member_ptr))>;
    int col_idx = static_cast<int>(I) + offset_;

    if (reader.is_null(col_idx)) {
        if constexpr (is_nullable_v<FieldType>) {
            entity.*(col.member_ptr) = std::nullopt;
        } else {
            entity.*(col.member_ptr) = FieldType{};
        }
        return;
    }

    if constexpr (is_nullable_v<FieldType>) {
        using InnerType = typename FieldType::value_type;
        if constexpr (std::is_same_v<InnerType, int>) {
            entity.*(col.member_ptr) = static_cast<int>(reader.get_int64(col_idx));
        } else if constexpr (std::is_same_v<InnerType, std::string>) {
            std::string_view sv = reader.get_string_view(col_idx);
            entity.*(col.member_ptr) = sv.data() != nullptr
                ? std::string(sv)
                : reader.get_string(col_idx);
        }
        // ...
    } else {
        if constexpr (std::is_same_v<FieldType, int>) {
            entity.*(col.member_ptr) = static_cast<int>(reader.get_int64(col_idx));
        } else if constexpr (std::is_same_v<FieldType, std::string>) {
            std::string_view sv = reader.get_string_view(col_idx);
            entity.*(col.member_ptr) = sv.data() != nullptr
                ? std::string(sv)
                : reader.get_string(col_idx);
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
| `std::vector<uint8_t>` | `BLOB`, `BYTEA`, `VARBINARY(MAX)` | Raw binary byte storage. `std::vector<char>` and `std::vector<std::byte>` are not mapped and fail the unsupported-type `static_assert`. |
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
| **`.to_list()`** | `ChunkedList<Entity, 64>` | Block-allocated chunks | **0 entity moves or copies** (in-place hydration); string/blob fields copied | Primary querying, sorting, ranges algorithms, high throughput |
| **`list.to_vector()`** | `std::vector<Entity>` | Contiguous array | Lvalue call copies every element; rvalue call (`std::move(list).to_vector()`) moves each element once | Legacy APIs or C-libraries requiring `data()` contiguous pointers |
| **`.first()`** | `std::optional<Entity>` | One 64-element chunk during execution | **1 move out of the chunk**, no copies | Finding by primary key, unique constraint, or existence check |
| **`.count()`** | `size_t` | $O(1)$ scalar | **0** | Pagination counters, cardinality checks |
| **`.stream()`** | `EntityStream<Entity, Mapper>` (move-only single-pass range) | $O(1)$ active row buffer; the driver may buffer the full result | **1 entity move per row**, plus field copies | Large datasets, continuous data processing, sequential transforms |
