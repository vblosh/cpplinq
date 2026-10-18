# SQL Functions, Aggregates & Window Functions

`cpplinq` provides cross-dialect support for standard SQL scalar functions, date/time operations, aggregate queries (`COUNT`, `SUM`, `AVG`, `MIN`, and `MAX`), `GROUP BY` and `HAVING` clauses, and window functions.

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

`cpplinq` abstracts year/month/day extraction, day addition, and current date/timestamp expressions across the supported dialects. SQLite uses `strftime` for extraction and `date(...)` for day addition; the latter returns a date and drops any time component. PostgreSQL uses `EXTRACT` with an `interval`, MSSQL uses `YEAR`/`MONTH`/`DAY` and `DATEADD`, MySQL uses `YEAR`/`MONTH`/`DAY` and `DATE_ADD`, Oracle uses `TO_TIMESTAMP`-based expressions, and Informix uses `YEAR`/`MONTH`/`DAY` with `UNITS DAY`. The expression API exposes only year, month, and day extraction (via `.year()`, `.month()`, `.day()`, `extract_year`, `extract_month`, or `extract_day`).

```cpp
// Current timestamp / date:
current_timestamp_val()
current_date_val()

// Date extraction (Year, Month, Day):
auto aug_events = db.from(events_table)
                    .where(events_table["created_at"].year() == 2026 &&
                           events_table["created_at"].month() == 8)
                    .to_list();

auto day_events = db.from(events_table)
                    .where(events_table["created_at"].day() == 16)
                    .to_list();

// Date arithmetic:
auto upcoming = db.from(events_table)
                  .where(events_table["created_at"].add_days(7) >= current_timestamp_val())
                  .to_list();
```

`current_timestamp_val()` and `current_date_val()` return `expr::Expr` values. There are no `now()`, `current_date()`, or `current_timestamp()` aliases.

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

`group_by()` and `having()` are available, but the expression API has no aggregate function builder. `ColumnHandle` has no `avg()`, and the aggregate terminals (`count()`, `avg()`, `sum()`, `min_val()`, and `max_val()`) execute separate queries and cannot be used in `having()`. Aggregate `HAVING` predicates therefore require constructing the expression AST manually or using raw SQL.

`to_list()` always projects every mapped column. In the example below that includes the non-grouped `salary` and `id` columns, which strict dialects such as PostgreSQL reject. Use raw SQL to select only grouped or aggregate columns when an aggregate projection is required.

```cpp
auto dept_groups = db.from(employees_table)
                     .group_by(employees_table["department"])
                     .order_by(employees_table["department"])
                     .to_list();
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
