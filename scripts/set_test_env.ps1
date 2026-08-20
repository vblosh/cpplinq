<#
.SYNOPSIS
    Configures environment variables for cpplinq integration tests and performance benchmarks.

.DESCRIPTION
    Sets the required and optional environment variables used by:
    - PostgreSQL Integration Tests & Benchmarks (CPPLINQ_POSTGRES_ODBC, CPPLINQ_POSTGRES_LIBPQ, PATH)
    - Microsoft SQL Server Integration Tests (CPPLINQ_MSSQL_ODBC)
    - MySQL / MariaDB Integration Tests (CPPLINQ_MYSQL_ODBC)
    - SQLite ODBC Integration Tests (CPPLINQ_SQLITE_ODBC)

.PARAMETER PostgresOdbc
    PostgreSQL ODBC DSN or connection string. Default: "PostgreSQL35W"

.PARAMETER PostgresLibpq
    Native PostgreSQL libpq connection string. Default: "host=localhost port=5432 dbname=cppdb user=cppdb password=cppdb_password"

.PARAMETER MssqlOdbc
    Microsoft SQL Server ODBC DSN or connection string. Default: "MSSQLLocalDB"

.PARAMETER MysqlOdbc
    MySQL / MariaDB ODBC DSN or connection string. Default: "MySQLDSN"

.PARAMETER SqliteOdbc
    SQLite ODBC connection string. Default: "" (optional)

.PARAMETER PostgresBinPath
    Path to PostgreSQL bin directory (containing libpq.dll). If empty, auto-detects from C:\Program Files\PostgreSQL\*.

.PARAMETER Scope
    Where to set environment variables: "Process" (current shell session, requires dot-sourcing) or "User" (persistent across all new terminal windows). Default: "Process"

.PARAMETER Permanent
    Switch shortcut for `-Scope User` to save variables permanently for the current user.

.PARAMETER Status
    Displays the current value of all CPPLINQ_* environment variables and PATH without changing them.

.PARAMETER Clear
    Unsets all CPPLINQ_* environment variables in the selected Scope.

.EXAMPLE
    # Set variables for the current PowerShell session (note the leading dot .):
    . .\scripts\set_test_env.ps1

.EXAMPLE
    # Set variables permanently for the current user:
    .\scripts\set_test_env.ps1 -Permanent

.EXAMPLE
    # Check currently active environment variables:
    .\scripts\set_test_env.ps1 -Status

.EXAMPLE
    # Custom PostgreSQL credentials:
    . .\scripts\set_test_env.ps1 -PostgresOdbc "PostgreSQL35W" -PostgresLibpq "host=localhost port=5432 dbname=mydb user=postgres password=secret"

.EXAMPLE
    # Clear all CPPLINQ environment variables:
    .\scripts\set_test_env.ps1 -Clear
#>

[CmdletBinding()]
param(
    [string]$PostgresOdbc = "PostgreSQL35W",
    [string]$PostgresLibpq = "host=localhost port=5432 dbname=cppdb user=cppdb password=cppdb_password",
    [string]$MssqlOdbc = "MSSQLLocalDB",
    [string]$MysqlOdbc = "MySQLDSN",
    [string]$OracleOdbc = "OracleDSN",
    [string]$InformixOdbc = "InformixDSN",
    [string]$SqliteOdbc = "",
    [string]$PostgresBinPath = "C:\Program Files\PostgreSQL\18\",
    [ValidateSet("Process", "User", "Machine")]
    [string]$Scope = "Process",
    [switch]$Permanent,
    [switch]$Status,
    [switch]$Clear
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ($Permanent) {
    $Scope = "User"
}

function Write-Info([string]$msg) {
    Write-Host "[INFO] $msg" -ForegroundColor Cyan
}

function Write-Success([string]$msg) {
    Write-Host "[SUCCESS] $msg" -ForegroundColor Green
}

function Write-Warn([string]$msg) {
    Write-Host "[WARN] $msg" -ForegroundColor Yellow
}

function Write-Highlight([string]$label, [string]$val) {
    Write-Host ("  {0,-28} = {1}" -f $label, $val) -ForegroundColor White
}

$varNames = @(
    "CPPLINQ_POSTGRES_ODBC",
    "CPPLINQ_POSTGRES_LIBPQ",
    "CPPLINQ_MSSQL_ODBC",
    "CPPLINQ_MYSQL_ODBC",
    "CPPLINQ_ORACLE_ODBC",
    "CPPLINQ_INFORMIX_ODBC",
    "CPPLINQ_SQLITE_ODBC"
)

# -----------------------------------------------------------------------------
# STATUS / SHOW
# -----------------------------------------------------------------------------
if ($Status) {
    Write-Info "Current CPPLINQ Environment Variables (Process Scope):"
    foreach ($var in $varNames) {
        $val = [System.Environment]::GetEnvironmentVariable($var, [System.EnvironmentVariableTarget]::Process)
        if ([string]::IsNullOrWhiteSpace($val)) {
            Write-Highlight $var "<not set>"
        } else {
            Write-Highlight $var $val
        }
    }

    Write-Host ""
    Write-Info "Current CPPLINQ Environment Variables (User Scope - Persistent):"
    foreach ($var in $varNames) {
        $val = [System.Environment]::GetEnvironmentVariable($var, [System.EnvironmentVariableTarget]::User)
        if ([string]::IsNullOrWhiteSpace($val)) {
            Write-Highlight $var "<not set>"
        } else {
            Write-Highlight $var $val
        }
    }

    Write-Host ""
    Write-Info "PostgreSQL in PATH:"
    $psqlCmd = Get-Command psql -ErrorAction SilentlyContinue
    if ($psqlCmd) {
        Write-Highlight "psql location" $psqlCmd.Source
    } else {
        Write-Highlight "psql location" "<not in PATH>"
    }
    return
}

# -----------------------------------------------------------------------------
# CLEAR / UNSET
# -----------------------------------------------------------------------------
if ($Clear) {
    Write-Info "Clearing CPPLINQ environment variables in Scope: $Scope"
    $target = [System.EnvironmentVariableTarget]::$Scope
    foreach ($var in $varNames) {
        [System.Environment]::SetEnvironmentVariable($var, $null, $target)
        if ($Scope -eq "Process") {
            Remove-Item "env:$var" -ErrorAction SilentlyContinue
        }
        Write-Success "Cleared $var"
    }
    return
}

# -----------------------------------------------------------------------------
# AUTO-DETECT POSTGRESQL BIN PATH
# -----------------------------------------------------------------------------
if ([string]::IsNullOrWhiteSpace($PostgresBinPath)) {
    $pgVersions = @(Get-ChildItem "C:\Program Files\PostgreSQL" -Directory -ErrorAction SilentlyContinue |
        Sort-Object { [int]($_.Name -replace '\D', '') } -Descending)

    if ($pgVersions.Count -gt 0) {
        $candidateBin = Join-Path $pgVersions[0].FullName "bin"
        if (Test-Path $candidateBin) {
            $PostgresBinPath = $candidateBin
        }
    }
}

# -----------------------------------------------------------------------------
# SET VARIABLES
# -----------------------------------------------------------------------------
$envMap = [ordered]@{
    "CPPLINQ_POSTGRES_LIBPQ" = $PostgresLibpq
    "CPPLINQ_MSSQL_ODBC"     = $MssqlOdbc
    "CPPLINQ_MYSQL_ODBC"     = $MysqlOdbc
    "CPPLINQ_ORACLE_ODBC"    = $OracleOdbc
    "CPPLINQ_INFORMIX_ODBC"  = $InformixOdbc

    #perfomance tests require PostgreSQL, so we only set this if it's not empty
    "CPPLINQ_POSTGRES_ODBC"  = $PostgresOdbc
    "PostgreSQL_ROOT" = $PostgresBinPath
}

if (-not [string]::IsNullOrWhiteSpace($SqliteOdbc)) {
    $envMap["CPPLINQ_SQLITE_ODBC"] = $SqliteOdbc
}

$target = [System.EnvironmentVariableTarget]::$Scope

Write-Info "Setting cpplinq test & benchmark environment variables (Scope: $Scope)..."
Write-Host ""

foreach ($entry in $envMap.GetEnumerator()) {
    [System.Environment]::SetEnvironmentVariable($entry.Key, $entry.Value, $target)
    if ($Scope -eq "Process") {
        Set-Item "env:$($entry.Key)" $entry.Value
    }
    Write-Highlight $entry.Key $entry.Value
}

# Configure PATH for libpq.dll if PostgreSQL bin directory is found
if (-not [string]::IsNullOrWhiteSpace($PostgresBinPath) -and (Test-Path $PostgresBinPath)) {
    $currentPath = [System.Environment]::GetEnvironmentVariable("PATH", $target)
    if ($currentPath -notmatch [regex]::Escape($PostgresBinPath)) {
        $newPath = "$PostgresBinPath;" + $currentPath
        [System.Environment]::SetEnvironmentVariable("PATH", $newPath, $target)
        if ($Scope -eq "Process") {
            $env:PATH = "$PostgresBinPath;" + $env:PATH
        }
        Write-Highlight "PATH (PostgreSQL)" "+ $PostgresBinPath"
    } else {
        Write-Highlight "PATH (PostgreSQL)" "Already present ($PostgresBinPath)"
    }
}

Write-Host ""
if ($Scope -eq "Process") {
    Write-Success "Environment variables set for current process!"
    Write-Warn "Note: If running in interactive PowerShell, ensure you dot-sourced the script: '. .\scripts\set_test_env.ps1'"
    Write-Info "To make these settings persistent across all new sessions, run: '.\scripts\set_test_env.ps1 -Permanent'"
} else {
    Write-Success "Environment variables saved permanently to User environment!"
    Write-Info "New terminal windows and applications will automatically have these variables."
}
