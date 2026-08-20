#!/usr/bin/env bash
# =============================================================================
# Run ODBC Integration Tests for cpplinq backends (MSSQL, PostgreSQL, MySQL, Informix, SQLite)
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR=""
BUILD_DIR_SPECIFIED=false
BUILD_TYPE="Release"
DATABASE="all"
USE_DOCKER=false
STOP_DOCKER=false
SKIP_BUILD=false
MSSQL_CONN=""
POSTGRES_CONN=""
MYSQL_CONN=""
ORACLE_CONN=""
INFORMIX_CONN=""
SQLITE_CONN=""

usage() {
    echo "Usage: $0 [OPTIONS]"
    echo "Options:"
    echo "  -d, --database <all|mssql|postgres|mysql|oracle|informix|sqlite>  Which database integration tests to run (default: all)"
    echo "  -b, --build-type <Release|Debug|RelWithDebInfo>                    CMake build type (default: Release)"
    echo "      --build-dir <dir>                                            Build directory (default: build)"
    echo "      --docker                                                     Start test databases using docker-compose.integration.yml"
    echo "      --stop-docker                                                Stop docker containers after test run"
    echo "      --skip-build                                                 Skip CMake configure and build"
    echo "      --mssql-conn <conn_str>                                      Override MSSQL connection string"
    echo "      --postgres-conn <conn_str>                                   Override PostgreSQL connection string"
    echo "      --mysql-conn <conn_str>                                      Override MySQL connection string"
    echo "      --oracle-conn <conn_str>                                     Override Oracle connection string"
    echo "      --informix-conn <conn_str>                                   Override Informix connection string"
    echo "      --sqlite-conn <conn_str>                                     Override SQLite connection string"
    echo "  -h, --help                                                       Show this help message"
    exit 1
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -d|--database) DATABASE="$(echo "$2" | tr '[:upper:]' '[:lower:]')"; shift 2 ;;
        -b|--build-type) BUILD_TYPE="$2"; shift 2 ;;
        --build-dir) BUILD_DIR="$2"; BUILD_DIR_SPECIFIED=true; shift 2 ;;
        --docker) USE_DOCKER=true; shift ;;
        --stop-docker) STOP_DOCKER=true; shift ;;
        --skip-build) SKIP_BUILD=true; shift ;;
        --mssql-conn) MSSQL_CONN="$2"; shift 2 ;;
        --postgres-conn) POSTGRES_CONN="$2"; shift 2 ;;
        --mysql-conn) MYSQL_CONN="$2"; shift 2 ;;
        --oracle-conn) ORACLE_CONN="$2"; shift 2 ;;
        --informix-conn) INFORMIX_CONN="$2"; shift 2 ;;
        --sqlite-conn) SQLITE_CONN="$2"; shift 2 ;;
        -h|--help) usage ;;
        *) echo "Unknown option: $1"; usage ;;
    esac
done

# Resolve default build directory if not explicitly specified
if [ "$BUILD_DIR_SPECIFIED" = false ]; then
    if [ -f "${PROJECT_ROOT}/build/CMakeCache.txt" ]; then
        # Check if the existing build directory is a Windows CMake cache or incompatible
        if grep -q "CMAKE_HOST_SYSTEM_NAME:STRING=Windows" "${PROJECT_ROOT}/build/CMakeCache.txt" 2>/dev/null || \
           grep -qE '^# For build in directory: [A-Za-z]:[/\\]' "${PROJECT_ROOT}/build/CMakeCache.txt" 2>/dev/null || \
           grep -qE '^[A-Za-z0-9_]+:[A-Za-z]+=([A-Za-z]:[/\\])' "${PROJECT_ROOT}/build/CMakeCache.txt" 2>/dev/null; then
            BUILD_DIR="${PROJECT_ROOT}/build-linux"
        else
            BUILD_DIR="${PROJECT_ROOT}/build"
        fi
    else
        BUILD_DIR="${PROJECT_ROOT}/build"
    fi
else
    # Make relative build path absolute if needed
    if [[ "$BUILD_DIR" != /* ]]; then
        BUILD_DIR="${PROJECT_ROOT}/${BUILD_DIR}"
    fi
fi

# Detect and configure local user-installed ODBC paths if available
if [ -d "$HOME/.local/usr/bin" ]; then
    case ":$PATH:" in
        *":$HOME/.local/usr/bin:"*) ;;
        *) export PATH="$HOME/.local/usr/bin:$HOME/.local/bin:$PATH" ;;
    esac
elif [ -d "$HOME/.local/bin" ]; then
    case ":$PATH:" in
        *":$HOME/.local/bin:"*) ;;
        *) export PATH="$HOME/.local/bin:$PATH" ;;
    esac
fi

ODBC_EXTRA_LIBS=()
[ -d "$HOME/.local/usr/lib/x86_64-linux-gnu" ] && ODBC_EXTRA_LIBS+=("$HOME/.local/usr/lib/x86_64-linux-gnu")
[ -d "$HOME/.local/opt/microsoft/msodbcsql18/lib64" ] && ODBC_EXTRA_LIBS+=("$HOME/.local/opt/microsoft/msodbcsql18/lib64")
for d in "$HOME/.local/opt/oracle"/instantclient_* "$HOME/.local/lib"/instantclient_* /opt/oracle/instantclient_*; do
    [ -d "$d" ] && ODBC_EXTRA_LIBS+=("$d") && break
done
if [ ${#ODBC_EXTRA_LIBS[@]} -gt 0 ]; then
    ODBC_LIB_STR="$(IFS=:; echo "${ODBC_EXTRA_LIBS[*]}")"
    case ":${LD_LIBRARY_PATH:-}:" in
        *":$ODBC_LIB_STR:"*) ;;
        *) export LD_LIBRARY_PATH="${ODBC_LIB_STR}:${LD_LIBRARY_PATH:-}" ;;
    esac
fi

if [ -f "$HOME/.local/etc/odbcinst.ini" ]; then
    export ODBCSYSINI="${ODBCSYSINI:-$HOME/.local/etc}"
    export ODBCINI="${ODBCINI:-$HOME/.local/etc/odbc.ini}"
fi

if [ -d "$HOME/.local/usr/lib/x86_64-linux-gnu/libmariadb3/plugin" ]; then
    export MARIADB_PLUGIN_DIR="${MARIADB_PLUGIN_DIR:-$HOME/.local/usr/lib/x86_64-linux-gnu/libmariadb3/plugin}"
fi

# Function to detect installed ODBC driver from a list of candidates
find_odbc_driver() {
    if ! command -v odbcinst >/dev/null 2>&1; then
        echo "$1"
        return
    fi
    local installed
    installed="$(odbcinst -q -d 2>/dev/null || true)"
    for d in "$@"; do
        if echo "$installed" | grep -qi "^\[\s*${d}\s*\]"; then
            echo "$d"
            return
        fi
    done
    echo "$1"
}

echo "========================================================"
echo ">> Setting up ODBC connection strings and services"
echo "========================================================"

if [ "$USE_DOCKER" = true ]; then
    echo "[INFO] Starting database containers with Docker Compose..."
    if ! docker compose -f "${SCRIPT_DIR}/docker-compose.integration.yml" up -d --wait 2>/dev/null; then
        docker compose -f "${SCRIPT_DIR}/docker-compose.integration.yml" up -d
        sleep 5
    fi
    docker exec cpplinq-postgres-test psql -U cppdb -d cppdb -c "DO \$\$ BEGIN IF NOT EXISTS (SELECT FROM pg_catalog.pg_roles WHERE rolname = 'cppdb') THEN CREATE ROLE cppdb WITH SUPERUSER LOGIN PASSWORD 'cppdb_password'; ELSE ALTER ROLE cppdb WITH SUPERUSER LOGIN PASSWORD 'cppdb_password'; END IF; END \$\$;" -c "SELECT 'CREATE DATABASE cppdb OWNER cppdb' WHERE NOT EXISTS (SELECT FROM pg_database WHERE datname = 'cppdb')\gexec" -c "GRANT ALL PRIVILEGES ON DATABASE cppdb TO cppdb;" 2>/dev/null || true
    docker exec cpplinq-mysql-test mysql -uroot -proot_password -e "CREATE DATABASE IF NOT EXISTS testdb; CREATE USER IF NOT EXISTS 'testuser'@'%' IDENTIFIED BY 'testpass'; ALTER USER 'testuser'@'%' IDENTIFIED BY 'testpass'; GRANT ALL PRIVILEGES ON *.* TO 'testuser'@'%' WITH GRANT OPTION; GRANT ALL PRIVILEGES ON *.* TO 'cppdb'@'%' WITH GRANT OPTION; FLUSH PRIVILEGES;" 2>/dev/null || true
    docker exec cpplinq-informix-test bash -c 'echo "CREATE DATABASE testdb WITH LOG;" | dbaccess - - 2>/dev/null || true' 2>/dev/null || true
fi

# Detect installed drivers
PG_DRIVER="$(find_odbc_driver "PostgreSQL Unicode" "PostgreSQL ANSI" "PostgreSQL Unicode(x64)" "PostgreSQL ANSI(x64)" "PostgreSQL")"
MSSQL_DRIVER="$(find_odbc_driver "ODBC Driver 18 for SQL Server" "ODBC Driver 17 for SQL Server" "FreeTDS")"
MYSQL_DRIVER="$(find_odbc_driver "MariaDB Unicode" "MySQL ODBC 8.0 Unicode Driver" "MySQL ODBC 8.0 ANSI Driver" "MySQL ODBC 9.0 Unicode Driver" "MariaDB ODBC 3.2 Driver" "MariaDB ODBC 3.1 Driver" "MySQL")"
ORACLE_DRIVER="$(find_odbc_driver "Oracle in OraDB23Home1" "Oracle in OraDB21Home1" "Oracle in OraClient19Home1" "Oracle in OraClient12Home1" "Oracle in instantclient_23_7" "Oracle ODBC Driver")"
INFORMIX_DRIVER="$(find_odbc_driver "IBM INFORMIX ODBC DRIVER (64-bit)" "IBM INFORMIX ODBC DRIVER" "IBM INFORMIX 3.82 32 BIT")"
SQLITE_DRIVER="$(find_odbc_driver "SQLite3" "SQLite" "SQLite3 ODBC Driver" "SQLite ODBC Driver")"

# Set connection strings with overrides or defaults
if [ -n "$POSTGRES_CONN" ]; then
    export CPPLINQ_POSTGRES_ODBC="$POSTGRES_CONN"
elif [ -z "${CPPLINQ_POSTGRES_ODBC:-}" ]; then
    export CPPLINQ_POSTGRES_ODBC="Driver={${PG_DRIVER}};Server=localhost;Port=5432;Database=cppdb;Uid=cppdb;Pwd=cppdb_password;"
fi
export CPPDB_POSTGRES_ODBC="$CPPLINQ_POSTGRES_ODBC"

if [ -n "$MSSQL_CONN" ]; then
    export CPPLINQ_MSSQL_ODBC="$MSSQL_CONN"
elif [ -z "${CPPLINQ_MSSQL_ODBC:-}" ]; then
    export CPPLINQ_MSSQL_ODBC="Driver={${MSSQL_DRIVER}};Server=127.0.0.1,1433;Database=master;Uid=sa;Pwd=Password123!;TrustServerCertificate=yes;"
fi
export CPPDB_MSSQL_ODBC="$CPPLINQ_MSSQL_ODBC"

if [ -n "$MYSQL_CONN" ]; then
    export CPPLINQ_MYSQL_ODBC="$MYSQL_CONN"
elif [ -z "${CPPLINQ_MYSQL_ODBC:-}" ]; then
    export CPPLINQ_MYSQL_ODBC="Driver={${MYSQL_DRIVER}};Server=127.0.0.1;Port=3306;Database=cppdb;Uid=cppdb;Pwd=cppdb_password;"
fi
export CPPDB_MYSQL_ODBC="$CPPLINQ_MYSQL_ODBC"

if [ -n "$ORACLE_CONN" ]; then
    export CPPLINQ_ORACLE_ODBC="$ORACLE_CONN"
elif [ -z "${CPPLINQ_ORACLE_ODBC:-}" ]; then
    export CPPLINQ_ORACLE_ODBC="Driver={${ORACLE_DRIVER}};Dbq=127.0.0.1:1521/FREEPDB1;Uid=cppdb;Pwd=cppdb_password;"
fi
export CPPDB_ORACLE_ODBC="$CPPLINQ_ORACLE_ODBC"

if [ -n "$INFORMIX_CONN" ]; then
    export CPPLINQ_INFORMIX_ODBC="$INFORMIX_CONN"
elif [ -z "${CPPLINQ_INFORMIX_ODBC:-}" ]; then
    export CPPLINQ_INFORMIX_ODBC="Driver={${INFORMIX_DRIVER}};Server=informix;Database=testdb;Host=127.0.0.1;Service=9088;Uid=informix;Pwd=in4mix;DELIMIDENT=Y;CLIENT_LOCALE=en_us.utf8;"
fi
export CPPDB_INFORMIX_ODBC="$CPPLINQ_INFORMIX_ODBC"

if [ -n "$SQLITE_CONN" ]; then
    export CPPLINQ_SQLITE_ODBC="$SQLITE_CONN"
elif [ -z "${CPPLINQ_SQLITE_ODBC:-}" ]; then
    export CPPLINQ_SQLITE_ODBC="Driver={${SQLITE_DRIVER}};Database=/tmp/cppdb.sqlite;"
fi
export CPPDB_SQLITE_ODBC="$CPPLINQ_SQLITE_ODBC"

echo "[INFO] Build Directory       = ${BUILD_DIR}"
echo "[INFO] CPPLINQ_POSTGRES_ODBC = ${CPPLINQ_POSTGRES_ODBC}"
echo "[INFO] CPPLINQ_MSSQL_ODBC    = ${CPPLINQ_MSSQL_ODBC}"
echo "[INFO] CPPLINQ_MYSQL_ODBC    = ${CPPLINQ_MYSQL_ODBC}"
echo "[INFO] CPPLINQ_ORACLE_ODBC   = ${CPPLINQ_ORACLE_ODBC}"
echo "[INFO] CPPLINQ_INFORMIX_ODBC = ${CPPLINQ_INFORMIX_ODBC}"
echo "[INFO] CPPLINQ_SQLITE_ODBC   = ${CPPLINQ_SQLITE_ODBC}"

if [ "$SKIP_BUILD" = false ]; then
    echo "========================================================"
    echo ">> Configuring & Building Integration Tests"
    echo "========================================================"

    # Check for incompatible CMake cache
    if [ -f "${BUILD_DIR}/CMakeCache.txt" ]; then
        if grep -q "CMAKE_HOST_SYSTEM_NAME:STRING=Windows" "${BUILD_DIR}/CMakeCache.txt" 2>/dev/null || \
           grep -qE '^# For build in directory: [A-Za-z]:[/\\]' "${BUILD_DIR}/CMakeCache.txt" 2>/dev/null || \
           grep -qE '^[A-Za-z0-9_]+:[A-Za-z]+=([A-Za-z]:[/\\])' "${BUILD_DIR}/CMakeCache.txt" 2>/dev/null; then
            echo "[WARN] Incompatible Windows CMake cache found in ${BUILD_DIR}. Cleaning cache..."
            rm -rf "${BUILD_DIR}/CMakeCache.txt" "${BUILD_DIR}/CMakeFiles"
        fi
    fi

    if [ ! -f "${BUILD_DIR}/CMakeCache.txt" ]; then
        CMAKE_EXTRA_ARGS=()
        if [ -d "$HOME/.local/usr/include" ] && [ -f "$HOME/.local/usr/lib/x86_64-linux-gnu/libodbc.so" ]; then
            CMAKE_EXTRA_ARGS+=(
                -DODBC_INCLUDE_DIR="$HOME/.local/usr/include"
                -DODBC_LIBRARY="$HOME/.local/usr/lib/x86_64-linux-gnu/libodbc.so"
            )
        fi

        cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}" \
            -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
            -DCPPLINQ_BUILD_TESTS=ON \
            -DCPPLINQ_ENABLE_SQLITE=ON \
            -DCPPLINQ_ENABLE_MSSQL=ON \
            -DCPPLINQ_ENABLE_POSTGRES=ON \
            -DCPPLINQ_ENABLE_MYSQL=ON \
            -DCPPLINQ_ENABLE_ORACLE=ON \
            -DCPPLINQ_ENABLE_INFORMIX=ON \
            "${CMAKE_EXTRA_ARGS[@]}"
    fi

    TARGET_ARGS=()
    if [[ "$DATABASE" == "all" || "$DATABASE" == "sqlite" ]]; then
        TARGET_ARGS+=(--target test_sqlite_integration --target test_connection_pool --target test_streaming)
    fi
    if [[ "$DATABASE" == "all" || "$DATABASE" == "mssql" ]]; then
        TARGET_ARGS+=(--target test_mssql_integration)
    fi
    if [[ "$DATABASE" == "all" || "$DATABASE" == "postgres" ]]; then
        TARGET_ARGS+=(--target test_postgres_integration)
    fi
    if [[ "$DATABASE" == "all" || "$DATABASE" == "mysql" ]]; then
        TARGET_ARGS+=(--target test_mysql_integration)
    fi
    if [[ "$DATABASE" == "all" || "$DATABASE" == "oracle" ]]; then
        TARGET_ARGS+=(--target test_oracle_query_builder --target test_oracle_integration)
    fi
    if [[ "$DATABASE" == "all" || "$DATABASE" == "informix" ]]; then
        TARGET_ARGS+=(--target test_informix_query_builder --target test_informix_integration)
    fi

    cmake --build "${BUILD_DIR}" --config "${BUILD_TYPE}" "${TARGET_ARGS[@]}" --parallel
fi

cleanup() {
    if [ "$STOP_DOCKER" = true ]; then
        echo "[INFO] Stopping Docker containers..."
        docker compose -f "${SCRIPT_DIR}/docker-compose.integration.yml" down
    fi
}
trap cleanup EXIT

echo "========================================================"
echo ">> Executing Integration Tests"
echo "========================================================"

TEST_RESULTS=()
ALL_PASSED=true

run_single_test() {
    local target="$1"
    local label="$2"
    local exe_path=""

    local candidate_paths=(
        "${BUILD_DIR}/tests/${BUILD_TYPE}/${target}"
        "${BUILD_DIR}/tests/${target}"
        "${BUILD_DIR}/${BUILD_TYPE}/${target}"
        "${BUILD_DIR}/${target}"
        "${BUILD_DIR}/bin/${BUILD_TYPE}/${target}"
        "${BUILD_DIR}/bin/${target}"
        "${BUILD_DIR}/tests/${BUILD_TYPE}/${target}.exe"
        "${BUILD_DIR}/tests/${target}.exe"
        "${BUILD_DIR}/${BUILD_TYPE}/${target}.exe"
        "${BUILD_DIR}/${target}.exe"
    )

    for p in "${candidate_paths[@]}"; do
        if [ -f "$p" ]; then
            chmod +x "$p" 2>/dev/null || true
            exe_path="$p"
            break
        fi
    done

    if [ -z "$exe_path" ]; then
        echo -e "\033[31m[ERROR] Could not find executable: ${target}\033[0m"
        TEST_RESULTS+=("${label}|MISSING|0.00")
        ALL_PASSED=false
        return
    fi

    echo ""
    echo -e "\033[35m>> Running ${label} (${exe_path})...\033[0m"
    local start_time
    start_time=$(date +%s%N 2>/dev/null || date +%s)
    local status="PASSED"
    if ! "$exe_path"; then
        status="FAILED"
        ALL_PASSED=false
    fi
    local end_time
    end_time=$(date +%s%N 2>/dev/null || date +%s)
    
    local duration="0.00"
    if [[ "$start_time" =~ ^[0-9]{10,}$ ]] && [[ "$end_time" =~ ^[0-9]{10,}$ ]]; then
        local diff_ns=$((end_time - start_time))
        local diff_ms=$((diff_ns / 1000000))
        duration=$(awk "BEGIN {printf \"%.2f\", $diff_ms/1000}" 2>/dev/null || echo "0.00")
    else
        local diff_s=$((end_time - start_time))
        duration="${diff_s}.00"
    fi

    TEST_RESULTS+=("${label}|${status}|${duration}")
}

if [[ "$DATABASE" == "all" || "$DATABASE" == "sqlite" ]]; then
    run_single_test "test_sqlite_integration" "SQLite Integration Tests"
    run_single_test "test_connection_pool" "Connection Pool Tests"
    run_single_test "test_streaming" "Streaming Query Tests"
fi

if [[ "$DATABASE" == "all" || "$DATABASE" == "mssql" ]]; then
    run_single_test "test_mssql_integration" "MSSQL Integration Tests"
fi

if [[ "$DATABASE" == "all" || "$DATABASE" == "postgres" ]]; then
    run_single_test "test_postgres_integration" "PostgreSQL Integration Tests"
fi

if [[ "$DATABASE" == "all" || "$DATABASE" == "mysql" ]]; then
    run_single_test "test_mysql_integration" "MySQL / MariaDB Integration Tests"
fi

if [[ "$DATABASE" == "all" || "$DATABASE" == "oracle" ]]; then
    run_single_test "test_oracle_query_builder" "Oracle Query Builder Tests"
    run_single_test "test_oracle_integration" "Oracle Integration Tests"
fi

if [[ "$DATABASE" == "all" || "$DATABASE" == "informix" ]]; then
    run_single_test "test_informix_query_builder" "Informix Query Builder Tests"
    run_single_test "test_informix_integration" "IBM Informix Integration Tests"
fi

echo ""
echo "========================================================"
echo ">> Integration Test Summary"
echo "========================================================"
printf "%-40s %-12s %-12s\n" "Suite" "Status" "DurationSec"
printf "%-40s %-12s %-12s\n" "----------------------------------------" "------------" "------------"
for res in "${TEST_RESULTS[@]}"; do
    IFS="|" read -r suite status duration <<< "$res"
    if [ "$status" = "PASSED" ]; then
        printf "%-40s \033[32m%-12s\033[0m %-12s\n" "$suite" "$status" "$duration"
    else
        printf "%-40s \033[31m%-12s\033[0m %-12s\n" "$suite" "$status" "$duration"
    fi
done

if [ "$ALL_PASSED" = true ]; then
    echo ""
    echo -e "\033[32m[SUCCESS] ALL INTEGRATION TESTS PASSED!\033[0m"
    exit 0
else
    echo ""
    echo -e "\033[31m[ERROR] SOME INTEGRATION TESTS FAILED!\033[0m"
    exit 1
fi

