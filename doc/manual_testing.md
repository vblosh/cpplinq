# Manual Testing & Building Guide

This guide covers building, running tests, and executing sanitizers on **Linux / WSL** and **Windows**. Sanitizer instructions apply to Linux; the Windows CI build uses MSVC Release without sanitizers.

---

## 1. Prerequisites

### Linux / WSL

Install the compiler, build tools, client libraries, and ODBC drivers used by the enabled backends:

```bash
sudo apt-get update && sudo apt-get install -y \
    build-essential \
    git \
    clang \
    cmake \
    ninja-build \
    unixodbc-dev \
    libsqliteodbc \
    odbc-postgresql \
    odbc-mariadb \
    default-mysql-client \
    postgresql-client \
    libpq-dev \
    libmariadb-dev
```

The PostgreSQL and MySQL backends require `libpq-dev` and `libmariadb-dev` at configure time when enabled. Install the Microsoft `msodbcsql18` package from the Microsoft package repository for SQL Server ODBC support. Oracle and Informix also require their vendor ODBC drivers and client libraries, which are not available from the default Ubuntu repositories.

### Windows

Install Visual Studio with the MSVC C++ toolchain, CMake, Ninja, Git, and the ODBC and client libraries for the backends you intend to build. The repository includes `scripts/setup_odbc_dsn.ps1` for the SQL Server and PostgreSQL user DSNs, plus an Oracle DSN when an Oracle driver is installed, and `scripts/set_test_env.ps1` for environment variables.

### Docker (for Integration Databases)

Docker Engine and the Docker Compose plugin are required for the container-based integration workflow. The Compose file is `scripts/docker-compose.integration.yml`.

---

## 2. Managing Test Database Containers

The Compose file defines three active services: PostgreSQL, MySQL, and SQL Server. Oracle and Informix service definitions are commented out, so Compose does not start them.

To start the available database containers:

```bash
docker compose -f scripts/docker-compose.integration.yml up -d
```

To verify container health:

```bash
docker compose -f scripts/docker-compose.integration.yml ps
```

To stop the containers:

```bash
docker compose -f scripts/docker-compose.integration.yml down
```

Oracle and Informix must be provisioned separately. Their integration tests skip when the corresponding connection variables are unset or the database cannot be reached, so a successful container-based run does not imply that those backends were tested.

---

## 3. Configuring Test Environment Variables

The environment scripts configure environment variables only. They do not register ODBC drivers or create DSNs. Create the DSNs first, then load the variables.

### Linux / WSL

```bash
./scripts/setup_odbc_dsn.sh
source ./scripts/set_test_env.sh
```

`setup_odbc_dsn.sh` writes the Linux ODBC configuration, including DSNs used by the default values. `set_test_env.sh` exports variables such as `CPPLINQ_POSTGRES_ODBC`, `CPPLINQ_POSTGRES_LIBPQ`, `CPPLINQ_MYSQL_ODBC`, `CPPLINQ_MYSQL_CLIENT`, `CPPLINQ_MSSQL_ODBC`, `CPPLINQ_ORACLE_ODBC`, `CPPLINQ_INFORMIX_ODBC`, and `CPPLINQ_SQLITE_ODBC`.

The Bash defaults include bare DSN names such as `PostgreSQL35W`, `MySQLDSN`, and `SQLite3`, plus full Oracle and Informix connection strings. The PowerShell defaults use bare names including `PostgreSQL35W`, `MySQLDSN`, `OracleDSN`, and `InformixDSN`. The ODBC DSN values only work when the matching DSN exists. The native PostgreSQL and MySQL drivers ignore bare DSNs; their tests use the `CPPLINQ_POSTGRES_LIBPQ` and `CPPLINQ_MYSQL_CLIENT` values that `set_test_env.sh` also exports.

### Windows (PowerShell)

```powershell
.\scripts\setup_odbc_dsn.ps1
. .\scripts\set_test_env.ps1
```

`setup_odbc_dsn.ps1` configures the `MSSQLLocalDB` and `PostgreSQL35W` user DSNs and, when a driver is available, `OracleDSN`. `set_test_env.ps1` exports the variables for the current process when dot-sourced. It leaves `CPPLINQ_SQLITE_ODBC` unset by default; the SQLite integration suite uses `:memory:` and does not need it. The remaining ODBC default DSN names still require matching DSNs to exist.

---

## 4. Automated Integration Test Runner

Use the helper script to build and run integration tests for any or all database backends:

```bash
# Run all integration tests
./scripts/run_odbc_integration_tests.sh

# Start the available Compose containers, run tests, and tear them down:
./scripts/run_odbc_integration_tests.sh --docker --stop-docker

# Run tests for a specific database backend:
./scripts/run_odbc_integration_tests.sh -d sqlite
./scripts/run_odbc_integration_tests.sh -d mysql
./scripts/run_odbc_integration_tests.sh -d postgres
./scripts/run_odbc_integration_tests.sh -d mssql
./scripts/run_odbc_integration_tests.sh -d oracle
./scripts/run_odbc_integration_tests.sh -d informix
```

`--docker` starts only the three active Compose services. Oracle and Informix tests still require externally provisioned databases and the corresponding environment variables.

---

## 5. Manual Build & Test (CMake / CTest)

### Standard Release Build (Linux / WSL)

```bash
# 1. Load environment
source ./scripts/set_test_env.sh

# 2. Configure
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$HOME/.local;$HOME/.local/usr" \
  -DCPPLINQ_ENABLE_SQLITE=ON \
  -DCPPLINQ_ENABLE_POSTGRES=ON \
  -DCPPLINQ_ENABLE_MSSQL=ON \
  -DCPPLINQ_ENABLE_MYSQL=ON \
  -DCPPLINQ_ENABLE_ORACLE=ON \
  -DCPPLINQ_ENABLE_INFORMIX=ON

# 3. Build
cmake --build build --parallel

# 4. Run all tests
ctest --test-dir build --output-on-failure
```

All six `CPPLINQ_ENABLE_*` backend options default to `ON`. Omitting them does not produce a minimal build: CMake still requires libpq, MySQL client libraries, unixODBC, and the other enabled dependencies. Set the backends you do not need to `OFF`.

### Windows (MSVC)

Run these commands from a Visual Studio developer shell that has `cl.exe` and Ninja on `PATH`:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The PowerShell integration runner can configure, build, and run the backend suites in one step:

```powershell
.\scripts\run_odbc_integration_tests.ps1 -Mode Local
```

Use `-Mode Docker -StopDockerOnExit` when the Compose databases are available. The POSIX paths in the remaining sections apply to Linux / WSL.

---

## 6. Building and Running with Clang + Sanitizers (Linux)

Use AddressSanitizer (ASan), UndefinedBehaviorSanitizer (UBSan), and LeakSanitizer (LSan) with Clang to detect memory errors, use-after-free, and undefined behavior. Linux CI runs an equivalent sanitizer build with GCC; Windows CI does not run sanitizers.

### 1. Configure with Clang Sanitizers

```bash
source ./scripts/set_test_env.sh

cmake -S . -B build-clang-asan -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER=clang \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g -Wall -Wextra" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined" \
  -DCMAKE_PREFIX_PATH="$HOME/.local;$HOME/.local/usr" \
  -DCPPLINQ_ENABLE_SQLITE=ON \
  -DCPPLINQ_ENABLE_POSTGRES=ON \
  -DCPPLINQ_ENABLE_MSSQL=ON \
  -DCPPLINQ_ENABLE_MYSQL=ON \
  -DCPPLINQ_ENABLE_ORACLE=ON \
  -DCPPLINQ_ENABLE_INFORMIX=ON
```

### 2. Build

```bash
cmake --build build-clang-asan --parallel
```

### 3. Run with Sanitizer Runtime Flags

```bash
export ASAN_OPTIONS="detect_leaks=1:halt_on_error=1"
export UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1"

ctest --test-dir build-clang-asan --output-on-failure
```

---

## 7. Running Individual Test Executables & Filters

Each enabled test target compiles to an independent executable in `<build_dir>/tests/`. The full target list is:

```bash
# Core unit tests
./build/tests/test_expression
./build/tests/test_query_builder
./build/tests/test_row_mapper
./build/tests/test_chunked_buffer
./build/tests/test_data_types

# ODBC helpers (when MSSQL, MySQL, Oracle, or Informix is enabled)
./build/tests/test_odbc_utils

# SQLite, pool, asynchronous pool, and streaming tests
./build/tests/test_sqlite_integration
./build/tests/test_connection_pool
./build/tests/test_async_connection_pool
./build/tests/test_streaming

# Per-backend query builders and integration suites
./build/tests/test_postgres_query_builder
./build/tests/test_postgres_integration
./build/tests/test_mssql_query_builder
./build/tests/test_mssql_integration
./build/tests/test_mysql_query_builder
./build/tests/test_mysql_integration
./build/tests/test_informix_query_builder
./build/tests/test_informix_integration
./build/tests/test_oracle_query_builder
./build/tests/test_oracle_integration
```

### Filtering specific tests with Google Test:
```bash
# Run a specific test case:
./build/tests/test_mysql_integration --gtest_filter="*DateTimeFunctions*"

# Run all transaction tests in MySQL suite:
./build/tests/test_mysql_integration --gtest_filter="*Transaction*"
```

---

## 8. Running Performance Benchmarks

Load the benchmark connection variables before running. The benchmark executables read the environment when they start and skip individual benchmarks when the required values are missing.

```bash
source ./scripts/set_test_env.sh

cmake -S . -B build-bench -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCPPLINQ_BUILD_BENCHMARKS=ON \
  -DCPPLINQ_BUILD_TESTS=OFF \
  -DCPPLINQ_ENABLE_POSTGRES=ON \
  -DCPPLINQ_ENABLE_INFORMIX=ON \
  -DCPPLINQ_ENABLE_SQLITE=OFF \
  -DCPPLINQ_ENABLE_MSSQL=OFF \
  -DCPPLINQ_ENABLE_MYSQL=OFF \
  -DCPPLINQ_ENABLE_ORACLE=OFF

cmake --build build-bench --parallel
```

```bash
# PostgreSQL: ORM/ODBC sections use CPPLINQ_POSTGRES_ODBC; native sections use CPPLINQ_POSTGRES_LIBPQ
./build-bench/benchmarks/bench_postgres_perf

# Informix: uses CPPLINQ_INFORMIX_ODBC or CPPDB_INFORMIX_ODBC
./build-bench/benchmarks/bench_informix_perf
```

When the MySQL backend is enabled, CMake also builds `bench_mysql_perf`, which reads `CPPLINQ_MYSQL_CLIENT` or `CPPLINQ_MYSQL_ODBC`. Missing variables cause the affected benchmarks to report a skip or error at runtime, not a build failure.
