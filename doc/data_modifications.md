# Data Modifications, Transactions & Connection Pooling

`cpplinq` provides high-performance data modification capabilities, cross-dialect UPSERT support, RAII transaction management, and a thread-safe connection pool.

---

## 1. Inserting Data

### 1.1 Single Insert

`insert()` returns the auto-increment primary key ID. SQLite and PostgreSQL use a `RETURNING` clause, SQL Server uses `OUTPUT INSERTED.<pk>`, and MySQL, Oracle, and Informix use a dialect-specific last-insert-ID query (Oracle uses `MAX(<pk>)` and Informix uses `DBINFO('sqlca.sqlerrd1')`):

```cpp
int64_t new_id = db.insert(users_table, User{0, "Alice", "alice@example.com", 30});
```

### 1.2 Batch Insert (`insert_many`)

Inserts a collection of entities inside an automatic transaction. When the driver reports array-batch support, rows are sent through its array binding API in chunks of the requested size. Otherwise, `insert_many` falls back to a single reused prepared INSERT executed row by row in one transaction; `chunk_size` is ignored on that fallback path.

`insert_many` accepts the entities by `const` reference and does not write generated IDs back into the vector. Auto-increment fields remain at their input values (the example below leaves them at `0`), so query the inserted rows or assign known primary keys before calling `update_many`.

```cpp
std::vector<User> batch = {
    {0, "Bob",   "bob@example.com",   25},
    {0, "Carol", "carol@example.com", 28},
    {0, "Dave",  "dave@example.com",  32}
};

// Insert in chunks of 50 when array batching is supported
db.insert_many(users_table, batch, 50);
```

---

## 2. Bulk & Batch Operations

### 2.1 Batch Update by Primary Key (`update_many`)

Updates an array of entities by matching their primary-key fields. Array-capable drivers batch the updates in chunks of the requested size; the row-by-row fallback ignores `chunk_size` and reuses one prepared UPDATE for each entity inside a single transaction. The entities must contain the IDs to match, because `insert_many` does not populate them:

```cpp
std::vector<User> updates = {
    {1, "Bob",   "bob@example.com",   26},
    {2, "Carol", "carol@example.com", 29},
    {3, "Dave",  "dave@example.com",  33}
};

// The IDs must already identify existing rows.
size_t updated_count = db.update_many(users_table, updates, 50);
```

### 2.2 Batch Delete by Primary Keys (`delete_many`)

Deletes rows by primary key in chunks. It supports exactly one primary-key column: `delete_many` throws `DbException` when the table has no primary key or when the key is composite.

```cpp
std::vector<int> user_ids = {1, 2, 3, 4, 5};
size_t deleted_count = db.delete_many(users_table, user_ids, 50);
```

### 2.3 Batch Upsert (`upsert_many`)

Atomically upserts a batch of entities in a transaction:

```cpp
std::vector<Account> upsert_batch = {
    Account{"user_0", "user0_new@example.com", 150},
    Account{"user_new", "new@example.com", 100}
};
db.upsert_many(accounts_table, upsert_batch);
```

### 2.4 Fast Table Truncate (`truncate`)

Clears all table rows quickly using `TRUNCATE TABLE` (with transparent `DELETE FROM` fallback):

```cpp
db.truncate(users_table);
```

---

## 3. Cross-Dialect UPSERT (`INSERT ON CONFLICT / MERGE`)

Performs an atomic insert-or-update operation across databases:

- **SQLite**: `INSERT INTO ... ON CONFLICT (cols) DO UPDATE SET ...`
- **PostgreSQL**: `INSERT INTO ... ON CONFLICT (cols) DO UPDATE SET ...`
- **Microsoft SQL Server**: `MERGE INTO ... USING (VALUES ...) ON ... WHEN MATCHED THEN UPDATE ... WHEN NOT MATCHED THEN INSERT ...`
- **MySQL / MariaDB**: `INSERT INTO ... VALUES (...) ON DUPLICATE KEY UPDATE ...`
- **Oracle**: `MERGE INTO ... USING (SELECT ... FROM DUAL) ... WHEN MATCHED THEN UPDATE ... WHEN NOT MATCHED THEN INSERT ...`
- **Informix**: `MERGE INTO ... USING (SELECT ... FROM "informix".systables WHERE tabid = 1) ... WHEN MATCHED THEN UPDATE ... WHEN NOT MATCHED THEN INSERT ...`

`conflict_columns` defaults to the column marked `primary_key`. If the table has none and no explicit list is supplied, SQL Server throws `std::invalid_argument` and Oracle returns an empty statement; the other `ON CONFLICT`/`MERGE` dialects generate invalid SQL. Pass the conflict columns explicitly for tables without a primary key.

```cpp
User user{1, "Alice Updated", "alice.new@example.com", 31};

// If ID=1 exists, update name, email, and age
db.upsert(
    users_table,
    user,
    {users_table["id"]},
    {users_table["name"], users_table["email"], users_table["age"]}
);
```

---

## 4. Updating & Removing via Query

### 4.1 Updating Data

Execute parameterized `UPDATE` statements on matching rows:

```cpp
size_t updated = db.from(users_table)
                   .where(users_table["id"] == 1)
                   .update({
                       users_table["email"] = "alice.verified@example.com",
                       users_table["age"]   = 31
                   });
```

### 4.2 Deleting Data

Execute parameterized `DELETE` statements on matching rows:

```cpp
size_t deleted = db.from(users_table)
                   .where(users_table["age"] < 18)
                   .remove();
```

---

## 5. RAII Transactions

`Transaction` ensures atomic commits and automatic rollbacks upon exception or early return:

```cpp
{
    auto txn = db.begin_transaction();

    db.insert(users_table, User{0, "Eve", "eve@example.com", 29});
    db.from(accounts_table)
      .where(accounts_table["user_id"] == 1)
      .update({accounts_table["balance"] = 1000.0});

    txn.commit(); // If omitted or if an exception is thrown, changes are rolled back automatically!
}
```

---

## 6. Thread-Safe Connection Pool

`ConnectionPool<Backend>` provides connection leasing and recycling for multi-threaded applications. `ConnectionPool<Backend>::create(...)` is the public factory and returns a `std::shared_ptr<ConnectionPool<Backend>>`; the constructor is private, so all pool operations go through the pointer:

```cpp
#include <cpplinq/driver/connection_pool.h>

PoolConfig config;
config.min_connections = 2;
config.max_connections = 10;
config.acquire_timeout = std::chrono::seconds(5);

auto pool = ConnectionPool<sqlite>::create("app.db", config);

// In a worker thread:
{
    // Lease connection with RAII guard:
    auto conn = pool->acquire();

    DbContext<sqlite> db(*conn);
    db.ensure_table(users_table);
    auto users = db.from(users_table).to_list();

    // conn is automatically returned to the pool when it goes out of scope!
}

// Pool Metrics:
std::cout << "Active: " << pool->active_count() << ", Idle: " << pool->idle_count() << "\n";
```
