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
