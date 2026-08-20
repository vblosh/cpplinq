#!/usr/bin/env bash
# =============================================================================
# Configure and verify ODBC DSNs on Linux for cpplinq integration tests
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

# Ensure ~/.local directories exist
mkdir -p "$HOME/.local/bin" "$HOME/.local/lib" "$HOME/.local/etc" "$HOME/.local/include"

if [ -d "$HOME/.local/usr/bin" ]; then
    case ":$PATH:" in
        *":$HOME/.local/usr/bin:"*) ;;
        *) export PATH="$HOME/.local/usr/bin:$HOME/.local/bin:$PATH" ;;
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

export ODBCSYSINI="${ODBCSYSINI:-$HOME/.local/etc}"
export ODBCINI="${ODBCINI:-$HOME/.local/etc/odbc.ini}"

# Find driver paths
PG_UNICODE_DRIVER=""
if [ -f "$HOME/.local/usr/lib/x86_64-linux-gnu/odbc/psqlodbcw.so" ]; then
    PG_UNICODE_DRIVER="$HOME/.local/usr/lib/x86_64-linux-gnu/odbc/psqlodbcw.so"
elif [ -f "/usr/lib/x86_64-linux-gnu/odbc/psqlodbcw.so" ]; then
    PG_UNICODE_DRIVER="/usr/lib/x86_64-linux-gnu/odbc/psqlodbcw.so"
fi

PG_ANSI_DRIVER=""
if [ -f "$HOME/.local/usr/lib/x86_64-linux-gnu/odbc/psqlodbca.so" ]; then
    PG_ANSI_DRIVER="$HOME/.local/usr/lib/x86_64-linux-gnu/odbc/psqlodbca.so"
elif [ -f "/usr/lib/x86_64-linux-gnu/odbc/psqlodbca.so" ]; then
    PG_ANSI_DRIVER="/usr/lib/x86_64-linux-gnu/odbc/psqlodbca.so"
fi

MSSQL_18_DRIVER=""
if [ -f "$HOME/.local/opt/microsoft/msodbcsql18/lib64/libmsodbcsql-18.6.so.1.1" ]; then
    MSSQL_18_DRIVER="$HOME/.local/opt/microsoft/msodbcsql18/lib64/libmsodbcsql-18.6.so.1.1"
elif [ -f "/opt/microsoft/msodbcsql18/lib64/libmsodbcsql-18.so" ]; then
    MSSQL_18_DRIVER="/opt/microsoft/msodbcsql18/lib64/libmsodbcsql-18.so"
fi

FREETDS_DRIVER=""
if [ -f "$HOME/.local/usr/lib/x86_64-linux-gnu/odbc/libtdsodbc.so" ]; then
    FREETDS_DRIVER="$HOME/.local/usr/lib/x86_64-linux-gnu/odbc/libtdsodbc.so"
elif [ -f "/usr/lib/x86_64-linux-gnu/odbc/libtdsodbc.so" ]; then
    FREETDS_DRIVER="/usr/lib/x86_64-linux-gnu/odbc/libtdsodbc.so"
fi

MARIADB_DRIVER=""
if [ -f "$HOME/.local/usr/lib/x86_64-linux-gnu/odbc/libmaodbc.so" ]; then
    MARIADB_DRIVER="$HOME/.local/usr/lib/x86_64-linux-gnu/odbc/libmaodbc.so"
elif [ -f "/usr/lib/x86_64-linux-gnu/odbc/libmaodbc.so" ]; then
    MARIADB_DRIVER="/usr/lib/x86_64-linux-gnu/odbc/libmaodbc.so"
fi

SQLITE_DRIVER=""
if [ -f "$HOME/.local/usr/lib/x86_64-linux-gnu/odbc/libsqlite3odbc-0.99991.so" ]; then
    SQLITE_DRIVER="$HOME/.local/usr/lib/x86_64-linux-gnu/odbc/libsqlite3odbc-0.99991.so"
elif [ -f "/usr/lib/x86_64-linux-gnu/odbc/libsqlite3odbc.so" ]; then
    SQLITE_DRIVER="/usr/lib/x86_64-linux-gnu/odbc/libsqlite3odbc.so"
fi

ORACLE_DRIVER=""
for f in \
    "$HOME/.local/opt/oracle"/instantclient_*/libsqora.so* \
    "$HOME/.local/lib"/instantclient_*/libsqora.so* \
    /opt/oracle/instantclient_*/libsqora.so* \
    "$HOME/.local/usr/lib/oracle/23/client64/lib/libsqora.so.23.1" \
    "$HOME/.local/usr/lib/oracle/21/client64/lib/libsqora.so.21.1" \
    "/usr/lib/oracle/23/client64/lib/libsqora.so.23.1" \
    "/usr/lib/oracle/21/client64/lib/libsqora.so.21.1"; do
    if [ -f "$f" ]; then
        ORACLE_DRIVER="$f"
        break
    fi
done

ODBCINST_LIB=""
if [ -f "$HOME/.local/usr/lib/x86_64-linux-gnu/libodbcinst.so" ]; then
    ODBCINST_LIB="$HOME/.local/usr/lib/x86_64-linux-gnu/libodbcinst.so"
elif [ -f "/usr/lib/x86_64-linux-gnu/libodbcinst.so" ]; then
    ODBCINST_LIB="/usr/lib/x86_64-linux-gnu/libodbcinst.so"
fi

cat << EOF > "$HOME/.local/etc/odbcinst.ini"
[PostgreSQL Unicode]
Description = PostgreSQL ODBC driver (Unicode)
Driver      = ${PG_UNICODE_DRIVER}
Setup       = ${ODBCINST_LIB}
Debug       = 0
CommLog     = 0

[PostgreSQL ANSI]
Description = PostgreSQL ODBC driver (ANSI)
Driver      = ${PG_ANSI_DRIVER}
Setup       = ${ODBCINST_LIB}
Debug       = 0
CommLog     = 0

[ODBC Driver 18 for SQL Server]
Description = Microsoft ODBC Driver 18 for SQL Server
Driver      = ${MSSQL_18_DRIVER}
UsageCount  = 1

[FreeTDS]
Description = FreeTDS ODBC Driver
Driver      = ${FREETDS_DRIVER}
Setup       = ${ODBCINST_LIB}
UsageCount  = 1

[MariaDB Unicode]
Description = MariaDB ODBC Driver (Unicode)
Driver      = ${MARIADB_DRIVER}
Setup       = ${ODBCINST_LIB}
PlugInDir   = $HOME/.local/usr/lib/x86_64-linux-gnu/libmariadb3/plugin
UsageCount  = 1

[MySQL ODBC 8.0 Unicode Driver]
Description = MySQL ODBC Driver (MariaDB compatible)
Driver      = ${MARIADB_DRIVER}
Setup       = ${ODBCINST_LIB}
PlugInDir   = $HOME/.local/usr/lib/x86_64-linux-gnu/libmariadb3/plugin
UsageCount  = 1

[Oracle in OraDB23Home1]
Description = Oracle ODBC Driver
Driver      = ${ORACLE_DRIVER}
Setup       = ${ODBCINST_LIB}
UsageCount  = 1

[SQLite3]
Description = SQLite3 ODBC Driver
Driver      = ${SQLITE_DRIVER}
Setup       = ${ODBCINST_LIB}
UsageCount  = 1
EOF

cat << 'EOF' > "$HOME/.local/etc/odbc.ini"
[PostgreSQL35W]
Description = PostgreSQL Database
Driver      = PostgreSQL Unicode
Servername  = 127.0.0.1
Port        = 5432
Database    = cppdb
UserName    = cppdb
Password    = cppdb_password

[PostgreSQL]
Description = PostgreSQL Database
Driver      = PostgreSQL Unicode
Servername  = 127.0.0.1
Port        = 5432
Database    = cppdb
UserName    = cppdb
Password    = cppdb_password

[MSSQLLocalDB]
Description            = Microsoft SQL Server
Driver                 = ODBC Driver 18 for SQL Server
Server                 = 127.0.0.1,1433
Database               = master
UID                    = sa
PWD                    = Password123!
TrustServerCertificate = yes

[MSSQLDSN]
Description            = Microsoft SQL Server
Driver                 = ODBC Driver 18 for SQL Server
Server                 = 127.0.0.1,1433
Database               = master
UID                    = sa
PWD                    = Password123!;
TrustServerCertificate = yes

[MySQLDSN]
Description = MySQL Database
Driver      = MariaDB Unicode
Server      = 127.0.0.1
Port        = 3306
Database    = cppdb
User        = cppdb
Password    = cppdb_password

[MySQL]
Description = MySQL Database
Driver      = MariaDB Unicode
Server      = 127.0.0.1
Port        = 3306
Database    = cppdb
User        = cppdb
Password    = cppdb_password

[OracleDSN]
Description = Oracle Database
Driver      = Oracle in OraDB23Home1
ServerName  = //127.0.0.1:1521/FREEPDB1
UserID      = cppdb
Password    = cppdb_password

[Oracle]
Description = Oracle Database
Driver      = Oracle in OraDB23Home1
ServerName  = //127.0.0.1:1521/FREEPDB1
UserID      = cppdb
Password    = cppdb_password

[SQLite3]
Description = SQLite3 Database
Driver      = SQLite3
Database    = /tmp/cppdb.sqlite
EOF

# Also keep ~/.odbcinst.ini and ~/.odbc.ini in sync
cp -f "$HOME/.local/etc/odbcinst.ini" "$HOME/.odbcinst.ini"
cp -f "$HOME/.local/etc/odbc.ini" "$HOME/.odbc.ini"

echo "[SUCCESS] Configured ODBC drivers in $HOME/.local/etc/odbcinst.ini and ~/.odbcinst.ini"
echo "[SUCCESS] Configured ODBC DSNs in $HOME/.local/etc/odbc.ini and ~/.odbc.ini"

if command -v odbcinst >/dev/null 2>&1; then
    echo ""
    echo ">> Registered ODBC Drivers:"
    odbcinst -q -d || true
    echo ""
    echo ">> Registered ODBC Data Sources (DSNs):"
    odbcinst -q -s || true
fi
