# SQL Functions, Aggregates & Window Functions

`cpplinq` provides cross-dialect support for standard SQL scalar functions, date/time operations, aggregate expressions with `GROUP BY` and `HAVING`, and window functions.

---

## 1. Built-in Scalar Functions

All scalar functions map transparently to dialect-appropriate SQL syntax (e.g. `LEN` and `SUBSTRING` on Microsoft SQL Server vs `LENGTH` and `SUBSTR` on SQLite/PostgreSQL).

```cpp
// String Functions:
upper(users_table["name"]) == "ALICE"
lower(users_table["email"]) == "test@example.com"
length(users_table["name"]) >= 5
trim(users_table["code"]) == "ABC"
substr(users_table["phone"], 1, 3) == "555"
coalesce(users_table["nickname"], users_table["name"])

// Math Functions:
abs_val(accounts_table["balance"]) > 1000.0
round_val(orders_table["amount"]) == 150.0
```

---

## 2. Date & Time Functions

`cpplinq` abstracts date extraction, arithmetic, and current timestamps across SQLite (`strftime`, `datetime`), PostgreSQL (`EXTRACT`, `interval`), and Microsoft SQL Server (`YEAR`, `MONTH`, `DAY`, `DATEADD`, `GETDATE`):

```cpp
// Current timestamp / date:
now()
current_date()
current_timestamp()

// Date extraction (Year, Month, Day):
auto aug_events = db.from(events_table)
                    .where(events_table["created_at"].year() == 2026 &&
                           events_table["created_at"].month() == 8)
                    .to_vector();

auto day_events = db.from(events_table)
                    .where(events_table["created_at"].day() == 16)
                    .to_vector();

// Date arithmetic:
auto upcoming = db.from(events_table)
                  .where(events_table["created_at"].add_days(7) >= now())
                  .to_vector();
```

---

## 3. Aggregates, GROUP BY & HAVING

### 3.1 Aggregations
```cpp
// Overall table aggregates:
size_t total_users    = db.from(users_table).count();
size_t distinct_names = db.from(users_table).count_distinct(users_table["name"]);
auto avg_salary       = db.from(employees_table).avg(employees_table["salary"]);
auto max_salary       = db.from(employees_table).max(employees_table["salary"]);
auto min_salary       = db.from(employees_table).min(employees_table["salary"]);
auto total_budget     = db.from(employees_table).sum(employees_table["salary"]);
```

### 3.2 GROUP BY & HAVING
```cpp
auto dept_groups = db.from(employees_table)
                     .group_by(employees_table["department"])
                     .having(employees_table["salary"].avg() > 75000.0)
                     .order_by(employees_table["department"])
                     .to_vector();
```

---

## 4. Window Functions

`cpplinq` provides a fluent syntax for SQL window functions with `OVER (PARTITION BY ... ORDER BY ...)`:

```cpp
// ROW_NUMBER() OVER (PARTITION BY department ORDER BY salary DESC)
auto rn_expr = row_number().over()
                           .partition_by(employees_table["department"])
                           .order_by(employees_table["salary"].desc());

// RANK() and DENSE_RANK()
auto rank_expr = rank().over().order_by(employees_table["salary"].desc());
auto dense_rank_expr = dense_rank().over().order_by(employees_table["salary"].desc());

// Aggregate Window Functions:
auto sum_dept = sum_over(employees_table["salary"]).over()
                                                   .partition_by(employees_table["department"]);

auto avg_dept = avg_over(employees_table["salary"]).over()
                                                   .partition_by(employees_table["department"]);

auto count_dept = count_over().over()
                              .partition_by(employees_table["department"]);
```
