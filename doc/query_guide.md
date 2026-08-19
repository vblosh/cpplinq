# Querying & Filtering Guide

`cpplinq` provides a type-safe fluent querying API. All query definitions are constructed using operator overloading and method chaining with deferred execution until a terminal method is called.

---

## 1. Defining Queries

Queries begin with `db.from(table)`:

```cpp
auto query = db.from(users_table);
```

---

## 2. Filtering (`WHERE`)

### 2.1 Comparisons
Supported operators: `==`, `!=`, `<`, `<=`, `>`, `>=`

```cpp
auto adults = db.from(users_table)
                .where(users_table["age"] >= 18)
                .to_list();
```

### 2.2 Boolean Logic
Chaining with `&&`, `||`, `!`:

```cpp
auto active_young_users = db.from(users_table)
                            .where((users_table["age"] >= 18 && users_table["age"] <= 30) || users_table["is_admin"] == true)
                            .to_list();
```

### 2.3 NULL Checks
```cpp
// IS NULL
auto unverified = db.from(users_table)
                    .where(users_table["email"].is_null())
                    .to_list();

// IS NOT NULL
auto verified = db.from(users_table)
                  .where(users_table["email"].is_not_null())
                  .to_list();
```

### 2.4 Range Testing (`BETWEEN`)
```cpp
auto prime_age = db.from(users_table)
                   .where(users_table["age"].between(25, 35))
                   .to_list();
```

### 2.5 Pattern Matching (`LIKE` & `NOT LIKE`)
```cpp
// Names starting with 'A'
auto a_users = db.from(users_table)
                 .where(users_table["name"].like("A%"))
                 .to_list();

// Emails not from spam.com
auto clean = db.from(users_table)
               .where(users_table["email"].not_like("%@spam.com"))
               .to_list();
```

### 2.6 List Containment (`IN` & `NOT IN`)
```cpp
// IN list of constants
auto selected = db.from(users_table)
                  .where(users_table["age"].in_list({20, 30, 40}))
                  .to_list();

// NOT IN list
auto rest = db.from(users_table)
              .where(users_table["age"].not_in_list({10, 20}))
              .to_list();
```

---

## 3. Sorting (`ORDER BY`)

Sorting supports multiple columns and directions:

```cpp
// Primary sort ASC, secondary sort DESC
auto sorted = db.from(users_table)
                .order_by(users_table["department"])
                .then_by_desc(users_table["salary"])
                .to_list();
```

Alternatively using `.asc()` / `.desc()`:
```cpp
auto sorted = db.from(users_table)
                .order_by(users_table["salary"].desc())
                .then_by(users_table["name"].asc())
                .to_list();
```

---

## 4. Pagination (`LIMIT` & `OFFSET`)

```cpp
// Get page 2 with page size 10 (records 11-20)
auto page2 = db.from(users_table)
               .order_by(users_table["id"])
               .limit(10)
               .offset(10)
               .to_list();
```

---

## 5. Distinct Queries

```cpp
auto distinct_users = db.from(users_table)
                        .distinct()
                        .to_list();
```

---

## 6. Terminal Operations

| Method | Return Type | Description |
|---|---|---|
| `.to_list()` | `ChunkedList<Entity, 64>` | Executes query and returns all matching entities zero-copy |
| `.stream()` | `EntityStream<Entity>` | Returns lazy C++20 input range for zero-heap streaming |
| `.first()` | `std::optional<Entity>` | Executes query with `LIMIT 1` and returns the first entity, or `std::nullopt` |
| `.count()` | `size_t` | Executes `SELECT COUNT(*)` |
| `.count_distinct(col)` | `size_t` | Executes `SELECT COUNT(DISTINCT col)` |
| `.sum(col)` | `std::optional<double>` | Executes `SELECT SUM(col)` |
| `.avg(col)` | `std::optional<double>` | Executes `SELECT AVG(col)` |
| `.min_val(col)` | `std::optional<double>` | Executes `SELECT MIN(col)` |
| `.max_val(col)` | `std::optional<double>` | Executes `SELECT MAX(col)` |
| `.update({...})` | `size_t` | Executes parameterized `UPDATE` and returns affected row count |
| `.remove()` | `size_t` | Executes parameterized `DELETE` and returns affected row count |
| `.prepare<Params...>()` | `PreparedQuery<Entity, ...>` | Compiles query once to a reusable prepared statement |
| `.prepare_update({...})` | `PreparedCommand<Params...>` | Compiles UPDATE statement once for reuse with parameters |
| `.prepare_remove()` | `PreparedCommand<Params...>` | Compiles DELETE statement once for reuse with parameters |

---

## 7. Prepared Statements & Parameter Reuse

For high-throughput applications, you can prepare a query template once and reuse it across multiple executions with different parameter values:

### 7.1 Type-Safe `PreparedQuery`
Use `cpplinq::param<T>(index)` to declare dynamic parameter placeholders:

```cpp
// 1. Prepare query once
auto find_users = db.from(users_table)
                    .where(users_table["age"] >= cpplinq::param<int>(0) &&
                           users_table["department"] == cpplinq::param<std::string>(1))
                    .order_by(users_table["age"].asc())
                    .prepare<int, std::string>();

// 2. Reuse repeatedly with zero query re-parsing overhead:
auto seniors = find_users.execute(30, "Engineering");
auto juniors = find_users.execute(21, "Sales");

// 3. First result and streaming:
auto first_lead = find_users.first(40, "Management");

for (const auto& user : find_users.stream(25, "Design")) {
    std::cout << user.name << "\n";
}
```

### 7.2 Reusable `UPDATE` and `DELETE` Commands
```cpp
// Reusable UPDATE
auto update_age = db.from(users_table)
                    .where(users_table["name"] == cpplinq::param<std::string>(0))
                    .prepare_update({ users_table["age"] = cpplinq::param<int>(1) });

update_age.execute("Alice", 31);
update_age.execute("Bob", 26);

// Reusable DELETE
auto delete_user = db.from(users_table)
                     .where(users_table["name"] == cpplinq::param<std::string>(0))
                     .prepare_remove();

delete_user.execute("Alice");
```

### 7.3 Raw Statement Reuse (`IPreparedStatement`)
When working directly with driver connections:

```cpp
auto stmt = conn.prepare("SELECT \"name\", \"age\" FROM \"users\" WHERE \"age\" >= ?");

stmt->bind(0, int64_t(20));
auto reader1 = stmt->execute_query();

// Reset and re-bind for next query:
stmt->reset();
stmt->bind(0, int64_t(30));
auto reader2 = stmt->execute_query();
```
