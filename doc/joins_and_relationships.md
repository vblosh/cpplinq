# Joins, Subqueries & CTEs Guide

`cpplinq` provides type-safe multi-table joins, subqueries, and Common Table Expressions (CTEs) that map SQL results directly to strongly-typed C++ tuples and pairs.

---

## 1. 2-Table Joins

### 1.1 INNER JOIN
Joins two tables on a predicate, returning `std::pair<Entity1, Entity2>`:

```cpp
auto user_orders = db.from(users_table)
                     .join(orders_table).on(users_table["id"] == orders_table["user_id"])
                     .where(orders_table["amount"] > 100.0)
                     .order_by_desc(orders_table["amount"])
                     .to_vector();

for (const auto& [user, order] : user_orders) {
    std::cout << user.name << " bought order #" << order.id << " ($" << order.amount << ")\n";
}
```

### 1.2 LEFT OUTER JOIN
Returns `std::pair<Entity1, std::optional<Entity2>>` where the joined entity is `std::nullopt` if no matching row exists:

```cpp
auto users_and_orders = db.from(users_table)
                          .left_join(orders_table).on(users_table["id"] == orders_table["user_id"])
                          .to_vector();

for (const auto& [user, order_opt] : users_and_orders) {
    if (order_opt.has_value()) {
        std::cout << user.name << " has order: " << order_opt->amount << "\n";
    } else {
        std::cout << user.name << " has NO orders\n";
    }
}
```

---

## 2. Multi-Table Joins (3+ Tables)

Chain multiple `.join()` and `.left_join()` calls. Results are mapped into a `std::tuple<Entity1, [optional<]Entity2[>], [optional<]Entity3[>]>`:

```cpp
// 3-Table Join: User + Order + Account
auto full_records = db.from(users_table)
                      .join(orders_table).on(users_table["id"] == orders_table["user_id"])
                      .join(accounts_table).on(users_table["id"] == accounts_table["user_id"])
                      .where(accounts_table["currency"] == "USD")
                      .to_vector();

for (const auto& [user, order, account] : full_records) {
    std::cout << user.name << " | Order: " << order.amount << " | Balance: " << account.balance << "\n";
}

// Mixed Left / Inner Joins:
auto user_orders_accounts = db.from(users_table)
                              .left_join(orders_table).on(users_table["id"] == orders_table["user_id"])
                              .join(accounts_table).on(users_table["id"] == accounts_table["user_id"])
                              .to_vector();

for (const auto& [user, order_opt, account] : user_orders_accounts) {
    std::cout << user.name << " (Account: " << account.currency << ") - ";
    if (order_opt) std::cout << "Order: " << order_opt->amount << "\n";
    else std::cout << "No orders\n";
}
```

---

## 3. Subqueries

`cpplinq` supports correlated subqueries, `EXISTS`, `NOT EXISTS`, `IN (subquery)`, and scalar subqueries.

### 3.1 `EXISTS` & `NOT EXISTS`
```cpp
// Users who have at least one order
auto active_buyers = db.from(users_table)
                       .where(exists(
                           db.from(orders_table)
                             .where(orders_table["user_id"] == users_table["id"])
                       ))
                       .to_vector();

// Users who have never placed an order
auto inactive_users = db.from(users_table)
                        .where(not_exists(
                            db.from(orders_table)
                              .where(orders_table["user_id"] == users_table["id"])
                        ))
                        .to_vector();
```

### 3.2 `IN (subquery)` & `NOT IN (subquery)`
```cpp
// Users who purchased items with amount > 500
auto high_spenders = db.from(users_table)
                       .where(users_table["id"].in(
                           db.from(orders_table)
                             .where(orders_table["amount"] > 500.0)
                             .as_subquery(orders_table["user_id"])
                       ))
                       .to_vector();
```

---

## 4. Common Table Expressions (CTEs: `WITH ... AS (...)`)

Define named subqueries that can be referenced in main queries:

```cpp
// Fluent QueryBuilder CTE:
auto sub = db.from(orders_table).where(orders_table["amount"] > 100.0);

auto users = db.from(users_table)
               .with_cte("high_value_orders", sub)
               .where(users_table["id"].in(
                   sub.as_subquery(orders_table["user_id"])
               ))
               .to_vector();
```

---

## 5. SQL Set Operations

Combine multiple queries with SQL set operations:

```cpp
auto q1 = db.from(table_a).where(table_a["status"] == "active");
auto q2 = db.from(table_b).where(table_b["status"] == "pending");

// UNION (distinct)
auto union_res = q1.union_with(q2).to_vector();

// UNION ALL
auto all_res = q1.union_all(q2).to_vector();

// INTERSECT
auto common_res = q1.intersect(q2).to_vector();

// EXCEPT (MINUS)
auto diff_res = q1.except_from(q2).to_vector();
```
