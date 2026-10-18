# Joins, Subqueries & CTEs Guide

`cpplinq` provides type-safe multi-table joins, subqueries, and Common Table Expressions (CTEs) that materialize SQL results directly into strongly-typed C++ tuples and pairs inside `ChunkedList` containers. Entities are constructed in place in chunked storage, while string and blob fields are copied from the driver into the entity.

---

## 1. 2-Table Joins

### 1.1 INNER JOIN

Joins two tables on a predicate, returning `ChunkedList<std::pair<Entity1, Entity2>>`:

```cpp
auto user_orders = db.from(users_table)
                     .join(orders_table).on(users_table["id"] == orders_table["user_id"])
                     .where(orders_table["amount"] > 100.0)
                     .order_by_desc(orders_table["amount"])
                     .to_list();

for (const auto& [user, order] : user_orders) {
    std::cout << user.name << " bought order #" << order.id << " ($" << order.amount << ")\n";
}
```

### 1.2 LEFT OUTER JOIN

Returns `ChunkedList<std::pair<Entity1, std::optional<Entity2>>>` where the joined entity is `std::nullopt` when the `LEFT JOIN` produces no matching row. Detection checks whether every projected column of the joined entity is SQL `NULL`, so a matched row whose mapped columns are all `NULL` is also reported as `std::nullopt`:

```cpp
auto users_and_orders = db.from(users_table)
                          .left_join(orders_table).on(users_table["id"] == orders_table["user_id"])
                          .to_list();

for (const auto& [user, order_opt] : users_and_orders) {
    if (order_opt.has_value()) {
        std::cout << user.name << " has order: " << order_opt->amount << "\n";
    } else {
        std::cout << user.name << " has NO orders\n";
    }
}
```

---

## 2. 3-Table Joins

Chain a second `.join()` / `.left_join()` after the first. Results are mapped into a `ChunkedList<std::tuple<Entity1, [optional<]Entity2[>], [optional<]Entity3[>]>>`:

> **Three tables is the limit.** The chain terminates at `Joined3QueryBuilder`, which has no further `.join()` / `.left_join()`. CTE and subquery builders cannot carry a join (see [Limitations](#limitations)), so joining a fourth physical table requires raw SQL.
>
> Joined builders expose `.where()`, `.order_by()` / `.order_by_desc()`, `.then_by()` / `.then_by_desc()`, `.limit()` / `.offset()`, `.to_list()`, `.first()`, and `.count()` — but not `.distinct()`, `.group_by()`, `.having()`, or `.stream()`. `first()` applies a limit of one; `count()` ignores `order_by`, `limit`, and `offset`.

```cpp
// 3-Table Join: User + Order + Account
auto full_records = db.from(users_table)
                      .join(orders_table).on(users_table["id"] == orders_table["user_id"])
                      .join(accounts_table).on(users_table["id"] == accounts_table["user_id"])
                      .where(accounts_table["currency"] == "USD")
                      .to_list();

for (const auto& [user, order, account] : full_records) {
    std::cout << user.name << " | Order: " << order.amount << " | Balance: " << account.balance << "\n";
}

// Mixed Left / Inner Joins:
auto user_orders_accounts = db.from(users_table)
                              .left_join(orders_table).on(users_table["id"] == orders_table["user_id"])
                              .join(accounts_table).on(users_table["id"] == accounts_table["user_id"])
                              .to_list();

for (const auto& [user, order_opt, account] : user_orders_accounts) {
    std::cout << user.name << " (Account: " << account.currency << ") - ";
    if (order_opt) std::cout << "Order: " << order_opt->amount << "\n";
    else std::cout << "No orders\n";
}
```

---

## 3. Subqueries

`cpplinq` supports correlated subqueries, `EXISTS`, `NOT EXISTS`, `IN (subquery)`, and scalar subqueries.

`ColumnHandle::in()` and `not_in()` require an `expr::SubqueryExpr`; they do **not** accept a `QueryBuilder`. Convert with `.as_subquery()` (all declared columns) or `.as_subquery(column)` (a single column). `exists()` and `not_exists()` accept either a `QueryBuilder` or a `SubqueryExpr`.

### 3.1 `EXISTS` & `NOT EXISTS`

```cpp
// Users who have at least one order
auto any_order = db.from(orders_table)
                   .where(orders_table["user_id"] == users_table["id"]);

auto active_buyers = db.from(users_table)
                       .where(exists(any_order.as_subquery()))
                       .to_list();

// Users who have never placed an order
auto inactive_users = db.from(users_table)
                        .where(not_exists(any_order.as_subquery()))
                        .to_list();
```

`EXISTS` renders the subquery's projected column list. `as_subquery()` projects every declared column, so those columns appear in the generated SQL even though `EXISTS` ignores their values; only a `SubqueryExpr` with an empty column list renders `SELECT 1`. The column choice does not affect the result, but it does affect the SQL text. (`as_subquery()` is reused for the second query — `SubqueryExpr` copies the `WHERE` clause, so `any_order` is not consumed.)

### 3.2 `IN (subquery)` & `NOT IN (subquery)`

```cpp
// Users who purchased items with amount > 500
auto big_spender_ids = db.from(orders_table)
                         .where(orders_table["amount"] > 500.0)
                         .as_subquery(orders_table["user_id"]);

auto high_spenders = db.from(users_table)
                       .where(users_table["id"].in(big_spender_ids))
                       .to_list();
```

The `IN` subquery must project exactly one column; `cpplinq` does not validate this. For `NOT IN`, use `users_table["id"].not_in(big_spender_ids)`.

### 3.3 Scalar Subqueries

A `SubqueryExpr` is not implicitly convertible to `Expr`, so wrap it explicitly. Once wrapped it renders as `(SELECT ...)` and can be used anywhere an `Expr` is accepted:

```cpp
auto max_amount_sub = db.from(orders_table).as_subquery(orders_table["amount"]);
expr::Expr max_amount(std::make_shared<expr::SubqueryExpr>(max_amount_sub));

auto above_max = db.from(orders_table)
                     .where(orders_table["amount"] >= max_amount)
                     .to_list();
```

The subquery must return at most one row and one column at runtime; `cpplinq` does not validate this.

---

## 4. Common Table Expressions (CTEs: `WITH ... AS (...)`)

`.with_cte(name, query)` adds a `WITH` clause to queries materialized through `to_list()`, `first()`, `stream()`, or `prepare()`. It does **not** change the outer query's `FROM` target — to read from the CTE, you must build a `TableDef` that names it and pass that to `db.from()`.

```cpp
// 1. Define the CTE source query. Only its projected columns, DISTINCT flag,
//    and WHERE clause are carried over (see Limitations below).
auto high_value = db.from(orders_table)
                    .where(orders_table["amount"] > 100.0);

// 2. Name the CTE. db.from() takes a TableDef, not a string, so wrap the
//    same column definitions in a table whose name matches the CTE.
auto high_value_orders = std::apply(
    [](auto... cols) { return table<Order>("high_value_orders", cols...); },
    orders_table.columns);

// 3. Query the CTE and attach its definition.
auto big_orders = db.from(high_value_orders)
                    .with_cte("high_value_orders", high_value)
                    .where(high_value_orders["amount"] > 500.0)
                    .to_list();
```

### Generated SQL

```sql
WITH "high_value_orders" AS (
    SELECT "id", "user_id", "amount"
    FROM "orders"
    WHERE ("orders"."amount" > ?)
)
SELECT "id", "user_id", "amount"
FROM "high_value_orders"
WHERE ("high_value_orders"."amount" > ?)
```

The CTE's column list is the *full* declared column list of the source table (`as_subquery()` projects every column) whenever the table has declared columns. A `TableDef` with no columns produces an empty list and therefore `SELECT *`. To project a single column instead, pass a `SubqueryExpr` explicitly:

```cpp
auto user_ids = db.from(orders_table).where(orders_table["amount"] > 100.0);

auto q = db.from(users_table)
           .with_cte("high_value_user_ids", user_ids.as_subquery(orders_table["user_id"]));
// WITH "high_value_user_ids" AS (SELECT "user_id" FROM "orders" WHERE ("orders"."amount" > ?)) ...
```

### Chaining Multiple CTEs

Multiple CTEs can be attached sequentially to a single `QueryBuilder`:

```cpp
auto q1 = db.from(users_table).where(users_table["age"] >= 21);
auto q2 = db.from(orders_table).where(orders_table["amount"] >= 500.0);

auto eligible_users = std::apply(
    [](auto... cols) { return table<User>("eligible_users", cols...); },
    users_table.columns);

auto results = db.from(eligible_users)
                 .with_cte("eligible_users", q1)
                 .with_cte("large_orders", q2)
                 .order_by(eligible_users["id"])
                 .to_list();
```

```sql
WITH "eligible_users" AS (SELECT "id", "name", "email", "age" FROM "users" WHERE ("users"."age" >= ?)),
     "large_orders" AS (SELECT "id", "user_id", "amount" FROM "orders" WHERE ("orders"."amount" >= ?))
SELECT "id", "name", "email", "age" FROM "eligible_users" ORDER BY "eligible_users"."id" ASC
```

Parameters are bound in definition order: all CTE parameters first (in the order the CTEs were added), then the outer query's parameters.

### Limitations

- **Where `.with_cte()` is available and when it survives:** it exists only on `QueryBuilder` (i.e. queries created by `db.from()`). Calling `.with_cte()` and then `.join()`, `.union_with()`, `.intersect()`, etc. **compiles**, but the joined/set-operation builders never receive the CTE list — the `WITH` clause is silently dropped from the generated SQL. The CTE list is also dropped by `count()`, `count_distinct()`, `avg()`, `sum()`, `min_val()`, `max_val()`, `exists()`, `update()`, `remove()`, `prepare_update()`, and `prepare_remove()`. Only `to_list()`, `first()`, `stream()`, and `prepare()` emit it.
- **What the CTE body preserves:** `SubqueryExpr` stores only `table_name`, the projected column list, `DISTINCT`, and `WHERE`. Any `order_by`, `limit`, `offset`, `group_by`, `having`, or joins on the source query are **silently dropped** when it is used as a CTE.
- **Referencing a CTE:** `db.from()` accepts only a `TableDef`, so referencing the CTE requires constructing one with the CTE's name (as shown above). The name given to `with_cte()` and the `TableDef` name must match.
- **Dialect support is not checked:** `DriverCapabilities::ctes` is informational; `with_cte()` will emit a `WITH` clause for any dialect.

---

## 5. SQL Set Operations

Combine multiple queries with SQL set operations:

```cpp
auto q1 = db.from(table_a).where(table_a["status"] == "active");
auto q2 = db.from(table_b).where(table_b["status"] == "pending");

// UNION (distinct)
auto union_res = q1.union_with(q2).to_list();

// UNION ALL
auto all_res = q1.union_all(q2).to_list();

// INTERSECT
auto common_res = q1.intersect(q2).to_list();

// EXCEPT
auto diff_res = q1.except_from(q2).to_list();
```

### Notes and caveats

- **`EXCEPT` vs `MINUS`:** `except_from()` always emits the keyword `EXCEPT`, and `intersect()` always emits `INTERSECT`. There is no dialect hook, so `except_from()` produces invalid SQL on Oracle, which requires `MINUS` instead. MySQL versions before 8.0.31 support neither `EXCEPT` nor `INTERSECT`. Use raw SQL there.
- **Same entity only:** every operand must be a `QueryBuilder` for the same `Entity` type. Column lists are taken from each table's declared columns, so a mismatch in column count or order produces invalid SQL at runtime with no compile-time check.
- **Right-hand query clauses are dropped:** only `table_name`, columns, `WHERE`, and `DISTINCT` are copied from the operand. Its `order_by`, `limit`, `offset`, `group_by`, `having`, and CTE list do not appear in the output.
- **Which clauses survive:** `SetOpQueryBuilder` supports `order_by` / `then_by`, `limit` / `offset`, `to_list`, `first`, and `count`. It has no `.where()`, `.distinct()`, `.group_by()`, or `.having()`. For `to_list()` and `first()`, `ORDER BY` and `LIMIT` apply to the combined result; `count()` ignores `order_by`, `limit`, and `offset`.
