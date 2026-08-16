# Data Modifications, Transactions & Connection Pooling

`cpplinq` provides high-performance data modification capabilities, cross-dialect UPSERT support, RAII transaction management, and a thread-safe connection pool.

---

## 1. Inserting Data

### 1.1 Single Insert
`insert()` returns the auto-increment primary key ID (using SQLite `last_insert_rowid()`, PostgreSQL `RETURNING id`, or MSSQL `OUTPUT INSERTED.id`):

```cpp
int64_t new_id = db.insert(users_table, User{0, "Alice", "alice@example.com", 30});
```

### 1.2 Batch Insert (`insert_many`)
Inserts a collection of entities inside a transaction automatically:

```cpp
std::vector<User> batch = {
    {0, "Bob",   "bob@example.com",   25},
    {0, "Carol", "carol@example.com", 28},
    {0, "Dave",  "dave@example.com",  32}
};

db.insert_many(users_table, batch);
```

---

## 2. Cross-Dialect UPSERT (`INSERT ON CONFLICT / MERGE`)

Performs an atomic insert-or-update operation across databases:
- **SQLite**: `INSERT INTO ... ON CONFLICT (cols) DO UPDATE SET ...`
- **PostgreSQL**: `INSERT INTO ... ON CONFLICT (cols) DO UPDATE SET ...`
- **Microsoft SQL Server**: `MERGE INTO ... USING (VALUES ...) ON ... WHEN MATCHED THEN UPDATE ... WHEN NOT MATCHED THEN INSERT ...`

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

## 3. Updating Data

Execute parameterized `UPDATE` statements on matching rows:

```cpp
size_t updated = db.from(users_table)
                   .where(users_table["id"] == 1)
                   .update({
                       users_table["email"] = "alice.verified@example.com",
                       users_table["age"]   = 31
                   });
```

---

## 4. Deleting Data

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

`ConnectionPool<Backend>` provides robust connection leasing and recycling for multi-threaded applications:

```cpp
#include <cpplinq/pool/connection_pool.h>

// Create a connection pool with min=2, max=10 connections, and a 5-second acquisition timeout
ConnectionPool<sqlite> pool(":memory:", 2, 10, std::chrono::seconds(5));

// In a worker thread:
{
    // Lease connection with RAII guard:
    auto conn = pool.acquire();

    DbContext<sqlite> db(*conn);
    auto users = db.from(users_table).to_vector();

    // conn is automatically returned to the pool when it goes out of scope!
}

// Pool Metrics:
std::cout << "Active: " << pool.active_count() << ", Idle: " << pool.idle_count() << "\n";
```
