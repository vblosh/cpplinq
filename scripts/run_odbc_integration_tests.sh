#!/usr/bin/env bash
# =============================================================================
# Run ODBC Integration Tests for Microsoft SQL Server and PostgreSQL (Linux/macOS/CI)
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
BUILD_TYPE="Release"
DATABASE="all"
USE_DOCKER=false
STOP_DOCKER=false

usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  -d, --database <all|mssql|postgres>  Which database integration tests to run (default: all)"
    echo "  -b, --build-type <Release|Debug>     CMake build type (default: Release)"
    echo "  --docker                             Start test databases using docker-compose.integration.yml"
    echo "  --stop-docker                        Stop docker containers after test run"
    echo "  --skip-build                         Skip CMake configure and build"
    echo "  -h, --help                           Show this help message"
    exit 1
}

SKIP_BUILD=false

while [[ $# -gt 0 ]]; do
    case "$1" in
        -d|--database) DATABASE="$2"; shift 2 ;;
        -b|--build-type) BUILD_TYPE="$2"; shift 2 ;;
        --docker) USE_DOCKER=true; shift ;;
        --stop-docker) STOP_DOCKER=true; shift ;;
        --skip-build) SKIP_BUILD=true; shift ;;
        -h|--help) usage ;;
        *) echo "Unknown option: $1"; usage ;;
    esac
done

echo "========================================================"
echo ">> Setting up ODBC connection strings and services"
echo "========================================================"

if [ "$USE_DOCKER" = true ]; then
    echo "[INFO] Starting database containers with Docker Compose..."
    docker compose -f "${SCRIPT_DIR}/docker-compose.integration.yml" up -d
fi

# Set default connection strings if not already exported in environment
if [ -z "${CPPLINQ_POSTGRES_ODBC:-}" ]; then
    export CPPLINQ_POSTGRES_ODBC="Driver={PostgreSQL Unicode};Server=localhost;Port=5432;Database=cppdb;Uid=cppdb;Pwd=cppdb_password;"
fi

if [ -z "${CPPLINQ_MSSQL_ODBC:-}" ]; then
    export CPPLINQ_MSSQL_ODBC="Driver={ODBC Driver 18 for SQL Server};Server=127.0.0.1,1433;Database=master;Uid=sa;Pwd=Password123!;TrustServerCertificate=yes;"
fi

echo "[INFO] CPPLINQ_POSTGRES_ODBC = ${CPPLINQ_POSTGRES_ODBC}"
echo "[INFO] CPPLINQ_MSSQL_ODBC    = ${CPPLINQ_MSSQL_ODBC}"

if [ "$SKIP_BUILD" = false ]; then
    echo "========================================================"
    echo ">> Configuring & Building Integration Tests"
    echo "========================================================"
    cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}" \
        -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
        -DCPPLINQ_BUILD_TESTS=ON \
        -DCPPLINQ_ENABLE_SQLITE=ON \
        -DCPPLINQ_ENABLE_MSSQL=ON \
        -DCPPLINQ_ENABLE_POSTGRES=ON

    TARGETS=()
    if [[ "$DATABASE" == "all" || "$DATABASE" == "mssql" ]]; then
        TARGETS+=(test_mssql_integration)
    fi
    if [[ "$DATABASE" == "all" || "$DATABASE" == "postgres" ]]; then
        TARGETS+=(test_postgres_integration)
    fi

    cmake --build "${BUILD_DIR}" --config "${BUILD_TYPE}" --target "${TARGETS[@]}" --parallel
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

if [[ "$DATABASE" == "all" || "$DATABASE" == "mssql" ]]; then
    echo "[RUN] MSSQL Integration Tests..."
    "${BUILD_DIR}/tests/test_mssql_integration"
fi

if [[ "$DATABASE" == "all" || "$DATABASE" == "postgres" ]]; then
    echo "[RUN] PostgreSQL Integration Tests..."
    "${BUILD_DIR}/tests/test_postgres_integration"
fi

echo "========================================================"
echo ">> All integration tests completed successfully!"
echo "========================================================"
