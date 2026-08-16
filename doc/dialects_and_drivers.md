# Database Dialects, Drivers & CI Configuration

`cpplinq` provides uniform cross-database abstractions across SQLite, PostgreSQL, and Microsoft SQL Server.

---

## 1. Supported Backends

### 1.1 SQLite
- **Backend Tag**: `cpplinq::sqlite`
- **Connection String**: File path (e.g. `"app.db"`) or in-memory (`":memory:"`).
- **Features**: Embedded WAL mode, thread-safe access, automatic schema generation (`AUTOINCREMENT`).

```cpp
auto db = cpplinq::connect<cpplinq::sqlite>("test.db");
```

### 1.2 PostgreSQL
- **Backend Tag**: `cpplinq::postgres`
- **Connection String**: ODBC DSN (e.g. `"PostgreSQL35W"` or standard connection strings).
- **Features**: Dollar-sign parameter binding (`$1`, `$2`), `RETURNING id`, `INTERVAL` date arithmetic, `EXTRACT` functions.

```cpp
auto db = cpplinq::connect<cpplinq::postgres>("Driver={PostgreSQL Unicode};Server=localhost;Port=5432;Database=cppdb;Uid=cppdb;Pwd=password;");
```

### 1.3 Microsoft SQL Server
- **Backend Tag**: `cpplinq::mssql` (or alias `cpplinq::sqlserver`)
- **Connection String**: ODBC connection string or DSN (e.g. `"MSSQLLocalDB"` or `"Driver={ODBC Driver 17 for SQL Server};Server=localhost;Database=testdb;Trusted_Connection=yes;"`).
- **Features**: Bracket identifier quoting (`[table].[column]`), `OUTPUT INSERTED.id` primary key retrieval, `OFFSET...FETCH NEXT` pagination, `MERGE INTO` UPSERT syntax.

```cpp
auto db = cpplinq::connect<cpplinq::mssql>("MSSQLLocalDB");
```

---

## 2. Environment Variables for Integration Tests

The integration test suites automatically discover connections via environment variables:

| Environment Variable | Target Database | Example Values |
|---|---|---|
| `CPPLINQ_POSTGRES_ODBC` / `CPPDB_POSTGRES_ODBC` | PostgreSQL | `"PostgreSQL35W"` or `"Driver={PostgreSQL Unicode};Server=localhost;Database=cppdb;Uid=postgres;Pwd=secret;"` |
| `CPPLINQ_MSSQL_ODBC` / `CPPDB_MSSQL_ODBC` | MSSQL Server | `"MSSQLLocalDB"` or `"Driver={ODBC Driver 17 for SQL Server};Server=(localdb)\MSSQLLocalDB;Database=master;Trusted_Connection=yes;"` |

If an environment variable is not defined, the corresponding integration test suite will be skipped cleanly with `GTEST_SKIP()`.

---

## 3. GitHub Actions CI Configuration

The GitHub Actions workflow runs testing across Linux and Windows with sanitizers (ASan & UBSan) and live PostgreSQL containers:

```yaml
name: CI

on:
  push:
    branches: [ master ]
  pull_request:
    branches: [ master ]

jobs:
  linux-asan-ubsan:
    name: Linux Sanitizers (ASan + UBSan)
    runs-on: ubuntu-latest
    services:
      postgres:
        image: postgres:16
        env:
          POSTGRES_USER: cppdb
          POSTGRES_PASSWORD: cppdb_password
          POSTGRES_DB: cppdb
        ports:
          - 5432:5432
        options: >-
          --health-cmd "pg_isready -U cppdb -d cppdb"
          --health-interval 10s
          --health-timeout 5s
          --health-retries 10
    steps:
      - uses: actions/checkout@v4
      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install --no-install-recommends -y \
            libgtest-dev libsqliteodbc ninja-build odbc-postgresql \
            postgresql-client unixodbc-dev
      - name: Configure environment
        run: |
          echo 'CPPDB_POSTGRES_ODBC=Driver={PostgreSQL Unicode};Server=localhost;Port=5432;Database=cppdb;Uid=cppdb;Pwd=cppdb_password;' >> "$GITHUB_ENV"
          echo "CPPDB_SQLITE_ODBC=Driver={SQLite3};Database=$RUNNER_TEMP/cppdb.sqlite;" >> "$GITHUB_ENV"
      - name: Build & Test
        run: |
          cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
          cmake --build build --parallel
          ctest --test-dir build --output-on-failure
```
