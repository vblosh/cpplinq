#!/usr/bin/env bash
# =============================================================================
# Configure environment variables for cpplinq integration tests & benchmarks
# =============================================================================
# Usage:
#   source ./scripts/set_test_env.sh
#   . ./scripts/set_test_env.sh
# =============================================================================

# Setup local ODBC paths if installed under ~/.local
if [ -d "$HOME/.local/usr/bin" ]; then
    case ":$PATH:" in
        *":$HOME/.local/usr/bin:"*) ;;
        *) export PATH="$HOME/.local/usr/bin:$PATH" ;;
    esac
fi

if [ -d "$HOME/.local/bin" ]; then
    case ":$PATH:" in
        *":$HOME/.local/bin:"*) ;;
        *) export PATH="$HOME/.local/bin:$PATH" ;;
    esac
fi

ODBC_LIB_DIRS=()
if [ -d "$HOME/.local/usr/lib/x86_64-linux-gnu" ]; then
    ODBC_LIB_DIRS+=("$HOME/.local/usr/lib/x86_64-linux-gnu")
fi
if [ -d "$HOME/.local/opt/microsoft/msodbcsql18/lib64" ]; then
    ODBC_LIB_DIRS+=("$HOME/.local/opt/microsoft/msodbcsql18/lib64")
fi
for d in "$HOME/.local/opt/oracle"/instantclient_* "$HOME/.local/lib"/instantclient_* /opt/oracle/instantclient_*; do
    [ -d "$d" ] && ODBC_LIB_DIRS+=("$d") && break
done

if [ ${#ODBC_LIB_DIRS[@]} -gt 0 ]; then
    ODBC_LIB_PATH="$(IFS=:; echo "${ODBC_LIB_DIRS[*]}")"
    case ":${LD_LIBRARY_PATH:-}:" in
        *":$ODBC_LIB_PATH:"*) ;;
        *) export LD_LIBRARY_PATH="${ODBC_LIB_PATH}:${LD_LIBRARY_PATH:-}" ;;
    esac
fi

if [ -f "$HOME/.local/etc/odbcinst.ini" ]; then
    export ODBCSYSINI="$HOME/.local/etc"
    export ODBCINI="$HOME/.local/etc/odbc.ini"
fi

if [ -d "$HOME/.local/usr/lib/x86_64-linux-gnu/libmariadb3/plugin" ]; then
    export MARIADB_PLUGIN_DIR="$HOME/.local/usr/lib/x86_64-linux-gnu/libmariadb3/plugin"
fi

if [ -z "${CPPLINQ_POSTGRES_ODBC:-}" ]; then
    export CPPLINQ_POSTGRES_ODBC="PostgreSQL35W"
fi
export CPPDB_POSTGRES_ODBC="$CPPLINQ_POSTGRES_ODBC"

if [ -z "${CPPLINQ_POSTGRES_LIBPQ:-}" ]; then
    export CPPLINQ_POSTGRES_LIBPQ="host=localhost port=5432 dbname=cppdb user=cppdb password=cppdb_password"
fi

if [ -z "${CPPLINQ_MSSQL_ODBC:-}" ]; then
    export CPPLINQ_MSSQL_ODBC="Driver={ODBC Driver 18 for SQL Server};Server=127.0.0.1,1433;Database=master;Uid=sa;Pwd=Password123!;TrustServerCertificate=yes;"
fi
export CPPDB_MSSQL_ODBC="$CPPLINQ_MSSQL_ODBC"

if [ -z "${CPPLINQ_MYSQL_ODBC:-}" ]; then
    export CPPLINQ_MYSQL_ODBC="MySQLDSN"
fi
export CPPDB_MYSQL_ODBC="$CPPLINQ_MYSQL_ODBC"

if [ -z "${CPPLINQ_MYSQL_CLIENT:-}" ]; then
    export CPPLINQ_MYSQL_CLIENT="host=127.0.0.1;port=3306;database=cppdb;user=cppdb;password=cppdb_password"
fi

if [ -d "$HOME/.local/usr/lib/x86_64-linux-gnu/pkgconfig" ]; then
    case ":${PKG_CONFIG_PATH:-}:" in
        *":$HOME/.local/usr/lib/x86_64-linux-gnu/pkgconfig:"*) ;;
        *) export PKG_CONFIG_PATH="$HOME/.local/usr/lib/x86_64-linux-gnu/pkgconfig:${PKG_CONFIG_PATH:-}" ;;
    esac
fi

if [ -d "$HOME/.local" ]; then
    export CMAKE_PREFIX_PATH="$HOME/.local;$HOME/.local/usr:${CMAKE_PREFIX_PATH:-}"
fi


if [ -z "${CPPLINQ_ORACLE_ODBC:-}" ]; then
    export CPPLINQ_ORACLE_ODBC="Driver={Oracle in OraDB23Home1};Dbq=127.0.0.1:1521/FREEPDB1;Uid=cppdb;Pwd=cppdb_password;"
fi
export CPPDB_ORACLE_ODBC="$CPPLINQ_ORACLE_ODBC"

if [ -z "${CPPLINQ_INFORMIX_ODBC:-}" ]; then
    export CPPLINQ_INFORMIX_ODBC="Driver={IBM INFORMIX ODBC DRIVER (64-bit)};Server=informix;Database=testdb;Host=127.0.0.1;Service=9088;Uid=informix;Pwd=in4mix;DELIMIDENT=Y;CLIENT_LOCALE=en_us.utf8;"
fi
export CPPDB_INFORMIX_ODBC="$CPPLINQ_INFORMIX_ODBC"

if [ -z "${CPPLINQ_SQLITE_ODBC:-}" ]; then
    export CPPLINQ_SQLITE_ODBC="SQLite3"
fi
export CPPDB_SQLITE_ODBC="$CPPLINQ_SQLITE_ODBC"

echo "[SUCCESS] cpplinq integration test environment configured:"
echo "  CPPLINQ_POSTGRES_ODBC = $CPPLINQ_POSTGRES_ODBC"
echo "  CPPLINQ_MSSQL_ODBC    = $CPPLINQ_MSSQL_ODBC"
echo "  CPPLINQ_MYSQL_ODBC    = $CPPLINQ_MYSQL_ODBC"
echo "  CPPLINQ_ORACLE_ODBC   = $CPPLINQ_ORACLE_ODBC"
echo "  CPPLINQ_INFORMIX_ODBC = $CPPLINQ_INFORMIX_ODBC"
echo "  CPPLINQ_SQLITE_ODBC   = $CPPLINQ_SQLITE_ODBC"
