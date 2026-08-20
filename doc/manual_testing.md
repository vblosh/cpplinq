# Manual Testing & Building Guide

This guide provides instructions for building, running tests, and executing sanitizers locally in **Linux / WSL** and **Windows**.

---

## 1. Prerequisites

### Linux / WSL
Ensure the following packages and dependencies are installed:
```bash
sudo apt-get update && sudo apt-get install -y \
    build-essential \
    clang \
    cmake \
    ninja-build \
    unixodbc-dev \
    libsqliteodbc \
    odbc-postgresql \
    odbc-mariadb \
    default-mysql-client \
    postgresql-client
```

### Docker (for Integration Databases)
Database integration tests require active database instances (PostgreSQL, MySQL, SQL Server, Oracle, Informix). A preconfigured Docker Compose file is located at `scripts/docker-compose.integration.yml`.

---

## 2. Managing Test Database Containers

To start all required database containers:
```bash
docker compose -f scripts/docker-compose.integration.yml up -d
```

To verify container health:
```bash
docker compose -f scripts/docker-compose.integration.yml ps
```

To stop containers when done:
```bash
docker compose -f scripts/docker-compose.integration.yml down
```

---

## 3. Configuring Test Environment Variables

Before running tests or CMake directly, configure the ODBC driver and database connection strings:

### Linux / WSL:
```bash
source ./scripts/set_test_env.sh
```

### Windows (PowerShell):
```powershell
. .\scripts\set_test_env.ps1
```

---

## 4. Automated Integration Test Runner

Use the provided helper script to automatically build and run integration tests for any or all database backends:

```bash
# Run all integration tests
./scripts/run_odbc_integration_tests.sh

# Automatically start Docker containers, run tests, and tear down:
./scripts/run_odbc_integration_tests.sh --docker --stop-docker

# Run tests for a specific database backend:
./scripts/run_odbc_integration_tests.sh -d sqlite
./scripts/run_odbc_integration_tests.sh -d mysql
./scripts/run_odbc_integration_tests.sh -d postgres
./scripts/run_odbc_integration_tests.sh -d mssql
./scripts/run_odbc_integration_tests.sh -d oracle
./scripts/run_odbc_integration_tests.sh -d informix
```

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

---

## 6. Building and Running with Clang + Sanitizers

Use AddressSanitizer (ASan), UndefinedBehaviorSanitizer (UBSan), and LeakSanitizer (LSan) with Clang to detect memory errors, use-after-free, and undefined behavior.

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
  -DCPPLINQ_ENABLE_ORACLE=ON
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

Each test target compiles to an independent executable in `<build_dir>/tests/`:

```bash
# Unit tests
./build/tests/test_expression
./build/tests/test_query_builder
./build/tests/test_row_mapper
./build/tests/test_chunked_buffer
./build/tests/test_streaming
./build/tests/test_connection_pool

# Database integration tests
./build/tests/test_sqlite_integration
./build/tests/test_postgres_integration
./build/tests/test_mssql_integration
./build/tests/test_mysql_integration
./build/tests/test_oracle_integration
./build/tests/test_informix_integration
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

To build and run the performance benchmarks:

```bash
cmake -S . -B build-bench -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCPPLINQ_BUILD_BENCHMARKS=ON \
  -DCPPLINQ_ENABLE_POSTGRES=ON \
  -DCPPLINQ_ENABLE_INFORMIX=ON

cmake --build build-bench --parallel

# Run PostgreSQL Benchmark
./build-bench/benchmarks/bench_postgres_perf

# Run Informix Benchmark
./build-bench/benchmarks/bench_informix_perf
```
