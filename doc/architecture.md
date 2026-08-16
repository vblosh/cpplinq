# `cpplinq` Architecture & Design

`cpplinq` is an expressive, type-safe LINQ-to-SQL style database access library built for modern C++ (C++20). It decouples query specification, SQL code generation, and database execution into distinct, composable layers.

```
┌─────────────────────────────────────────────────────────────┐
│                       User Entity & Schema                  │
│               struct User { ... }; table<User>(...)         │
└──────────────────────────────┬──────────────────────────────┘
                               │
┌──────────────────────────────▼──────────────────────────────┐
│                    Type-Safe Expression AST                 │
│         users["age"] > 18 && users["name"].like("A%")       │
└──────────────────────────────┬──────────────────────────────┘
                               │
┌──────────────────────────────▼──────────────────────────────┐
│             QueryBuilder & SetOp / Joined Builder           │
│   .where(...) .order_by(...) .join(...).on(...) .with_cte() │
└──────────────────────────────┬──────────────────────────────┘
                               │
┌──────────────────────────────▼──────────────────────────────┐
│                       SqlGenerator                          │
│               Translates AST to Parameterized SQL           │
└──────────────┬───────────────┼───────────────┬──────────────┘
               │               │               │
      ┌────────▼────────┐ ┌────▼─────┐ ┌───────▼────────┐
      │  ISqlDialect    │ │PostgreSQL│ │Microsoft SQL   │
      │    (SQLite)     │ │ Dialect  │ │Server Dialect  │
      └────────┬────────┘ └────┬─────┘ └───────┬────────┘
               │               │               │
┌──────────────▼───────────────▼───────────────▼──────────────┐
│                       Driver Layer                          │
│          IConnection / IPreparedStatement / IDataReader     │
└──────────────────────────────┬──────────────────────────────┘
                               │
┌──────────────────────────────▼──────────────────────────────┐
│                       RowMapper                             │
│     Direct struct materialization via compile-time reflection│
└─────────────────────────────────────────────────────────────┘
```

---

## 1. Core Layers

### 1.1 Compile-Time Schema Mapping
- **`ColumnDef<Entity, FieldType>`**: Holds member pointers (`&User::id`) and constraint tags (`primary_key`, `auto_increment`, `not_null`, `unique_col`).
- **`TableDef<Entity, ColumnDefs...>`**: Encapsulates table name and a compile-time `std::tuple` of columns. Provides column indexing via string (`table["col_name"]`).
- **`SqlType` & Type Traits**: Maps C++ fundamental types and `std::optional<T>` to database types with zero runtime overhead.

### 1.2 Expression Tree AST Engine
Instead of building SQL strings directly, C++ operators (`==`, `!=`, `<`, `<=`, `>`, `>=`, `&&`, `||`, `!`) instantiate lightweight, immutable AST nodes (`ExprNode`).
Nodes support:
- Column references (`ColumnRef`)
- Literal constants (`SqlValue` / `BoundValue`)
- Binary comparisons and logical gates (`BinaryExpr`, `LogicExpr`)
- Unary predicates (`IsNullExpr`, `IsNotNullExpr`, `NotExpr`)
- Pattern matching (`LikeExpr`)
- Range testing (`BetweenExpr`)
- Set inclusion (`InListExpr`, `InSubqueryExpr`)
- Scalar & math functions (`FunctionExpr`)
- Cross-dialect Date/Time operations (`ExtractExpr`, `DateAddExpr`, `CurrentTimestampExpr`, `CurrentDateExpr`)
- Subqueries (`ExistsExpr`, `SubqueryExpr`)
- Window functions (`WindowExpr`)

### 1.3 Dialect Abstraction & SQL Generator
- **`ISqlDialect`**: Encapsulates database-specific syntax differences:
  - Identifier quoting (`"table"` for SQLite/Postgres vs `[table]` for MSSQL)
  - Parameter placeholders (`?` vs `$1` vs `@p1`)
  - Auto-increment identity generation (`AUTOINCREMENT`, `SERIAL`, `IDENTITY(1,1)`)
  - Primary key retrieval (`last_insert_rowid()`, `RETURNING id`, `OUTPUT INSERTED.id`)
  - Pagination (`LIMIT...OFFSET` vs `OFFSET...FETCH NEXT`)
  - Upsert syntax (`INSERT ON CONFLICT DO UPDATE` vs `MERGE INTO`)
  - Date & time functions (`strftime`, `EXTRACT`, `DATEADD`)
- **`SqlGenerator`**: Walks the expression AST using `std::visit` and produces parameterized SQL queries with separate bound value vectors, preventing SQL injection vulnerabilities.

### 1.4 Database Driver Abstraction
- **`IConnection`**: Abstract connection interface managing transactions and statement preparation.
- **`IPreparedStatement`**: Handles type-erased parameter binding (`bind(index, BoundValue)`).
- **`IDataReader`**: Cursor-based result reader providing type-safe column extraction (`get_string`, `get_int64`, `get_double`, `is_null`, etc.).

### 1.5 Row Mapper
- **`RowMapper<Entity, ColumnDefs...>`**: Uses C++20 template meta-programming and `std::index_sequence` to unpack cursor columns directly into user struct fields by member pointer without runtime overhead, allocations, or dynamic dispatch.
- **Tuple & Optional Mapping**: Automatically unpacks `std::pair<E1, E2>`, `std::pair<E1, std::optional<E2>>`, and `std::tuple<E1, E2, E3>` for multi-table and left outer joins.

### 1.6 Thread-Safe Connection Pool
- **`ConnectionPool<Backend>`**: Manages a thread-safe pool of live database connections with RAII leasing (`PooledConnection`), acquisition timeouts, idle recycling, and connection health checks.
