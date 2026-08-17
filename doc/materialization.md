# Row Materialization & Memory Management Guide

`cpplinq` uses compile-time metaprogramming and member pointer reflection to materialize database rows into strongly-typed C++ structs, pairs, and tuples with **zero runtime reflection overhead, zero intermediate string parsing, and zero redundant memory reallocations**.

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
       ┌─────────────────────────┼─────────────────────────┐
       ▼                         ▼                         ▼
.to_vector()                  .first()                 .stream()
(ChunkedBuffer -> vector)     (Single std::optional)   (Lazy C++20 Range)
```

---

## 2. Materialization Methods

### 2.1 `.to_vector()` — Complete Collection Materialization

Executes the query and materializes all matching database rows into a contiguous `std::vector<Entity>` (or `std::vector<std::pair<...>>` / `std::vector<std::tuple<...>>` for joins).

```cpp
auto users = db.from(users_table)
               .where(users_table["age"] >= 18)
               .order_by(users_table["name"])
               .to_vector();
```

#### High-Performance Zero-Reallocation Architecture:
Standard `std::vector::push_back` causes $O(\log N)$ memory reallocations during cursor iteration, repeatedly moving and copying all previously fetched structs. `cpplinq` avoids this through a dual-path engine:

1. **Known Limit Fast-Path**: If `.limit(N)` is defined on the query, `cpplinq` immediately calls `results.reserve(*limit_)`. The destination vector is allocated **exactly once** upfront.
2. **Chunked Buffer Architecture (`ChunkedBuffer<Entity, 64>` / `ChunkedList<Entity, 64>`)**: When row count is unknown upfront, `cpplinq` uses a block-allocated chunk buffer:
   - Rows are constructed in-place into fixed 64-element blocks.
   - When a block fills up, a new block is allocated; **existing elements are never moved or copied** while streaming from the database.
   - Once cursor iteration finishes and the exact count $N$ is known, `cpplinq` allocates the final `std::vector<Entity>` **exactly once** and moves elements sequentially in a single cache-friendly pass.

```cpp
// Direct usage of ChunkedBuffer / ChunkedList as a standalone container:
cpplinq::ChunkedList<User, 64> buffer;
buffer.emplace_back(1, "Alice", "alice@example.com", 30);
buffer.emplace_back(2, "Bob", "bob@example.com", 25);

// 1. Forward iteration:
for (const auto& user : buffer) { /* ... */ }

// 2. Reverse iteration:
for (auto it = buffer.rbegin(); it != buffer.rend(); ++it) { /* ... */ }

// 3. Random access iterators & standard algorithms:
std::sort(buffer.begin(), buffer.end(), [](const auto& a, const auto& b) {
    return a.age < b.age;
});

// 4. Element access:
User& first_user = buffer[0];
User& checked_user = buffer.at(1);
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

When joining tables, `cpplinq` inspects join types at compile time and materializes appropriate compound types:

### Inner Joins $\to$ `std::pair<E1, E2>` / `std::tuple<E1, E2, E3>`
```cpp
// 2 Tables Inner Join
std::vector<std::pair<User, Order>> pairs = 
    db.from(users_table)
      .inner_join(orders_table).on(users_table["id"] == orders_table["user_id"])
      .to_vector();

// 3 Tables Inner Join
std::vector<std::tuple<User, Order, Item>> triplets = 
    db.from(users_table)
      .inner_join(orders_table).on(users_table["id"] == orders_table["user_id"])
      .inner_join(items_table).on(orders_table["id"] == items_table["order_id"])
      .to_vector();
```

### Left Outer Joins $\to$ `std::pair<E1, std::optional<E2>>`
For outer joins where the right-hand entity may not match, `cpplinq` wraps the joined entity in `std::optional`:

```cpp
std::vector<std::pair<User, std::optional<Order>>> results = 
    db.from(users_table)
      .left_join(orders_table).on(users_table["id"] == orders_table["user_id"])
      .to_vector();

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

### Supported Types:
- **Integers**: `int`, `int64_t`, `int32_t`, `uint32_t`, `uint64_t`
- **Floating-Point**: `double`, `float`
- **Strings**: `std::string`
- **Booleans**: `bool`
- **Binary Data**: `std::vector<uint8_t>` (BLOB)
- **Nullables**: `std::optional<T>` for any of the above

---

## 5. Comparison Matrix of Materialization Methods

| Method | Return Type | Memory Footprint | Reallocations | Best Use Case |
|---|---|---|---|---|
| **`.to_vector()`** | `std::vector<Entity>` | Contiguous buffer of all rows | **0** during fetch (via `ChunkedBuffer` or `.limit()`) | Standard querying, small-to-medium result sets, random access |
| **`.first()`** | `std::optional<Entity>` | $O(1)$ single struct | **0** | Finding by primary key, unique constraint, or existence check |
| **`.count()`** | `size_t` | $O(1)$ scalar | **0** | Pagination counters, cardinality checks |
| **`.stream()`** | `EntityStream` (C++20 Range) | $O(1)$ active row buffer | **0** (no vector allocated) | Large datasets, continuous data processing, pipeline transforms |
