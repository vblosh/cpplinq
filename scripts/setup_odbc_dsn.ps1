<#
.SYNOPSIS
    Configures and verifies ODBC DSNs on Windows for cpplinq integration tests (MSSQL & PostgreSQL).

.DESCRIPTION
    This script inspects the system for available 64-bit and 32-bit ODBC drivers for Microsoft SQL Server
    and PostgreSQL, and configures User DSNs (`MSSQLLocalDB` and `PostgreSQL35W`) or outputs connection strings.

.PARAMETER Clean
    Removes the test DSNs created for cpplinq.

.PARAMETER ShowDrivers
    Displays all installed ODBC drivers.

.PARAMETER Force
    Overwrites existing DSNs if they already exist.

.EXAMPLE
    .\scripts\setup_odbc_dsn.ps1
    .\scripts\setup_odbc_dsn.ps1 -ShowDrivers
    .\scripts\setup_odbc_dsn.ps1 -Clean
#>

[CmdletBinding()]
param(
    [switch]$Clean,
    [switch]$ShowDrivers,
    [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Write-Info([string]$msg) {
    Write-Host "[INFO] $msg" -ForegroundColor Cyan
}

function Write-Success([string]$msg) {
    Write-Host "[SUCCESS] $msg" -ForegroundColor Green
}

function Write-Warn([string]$msg) {
    Write-Host "[WARN] $msg" -ForegroundColor Yellow
}

function Write-Err([string]$msg) {
    Write-Host "[ERROR] $msg" -ForegroundColor Red
}

# 1. Show drivers if requested
if ($ShowDrivers) {
    Write-Info "Installed ODBC Drivers (64-bit & 32-bit):"
    Get-OdbcDriver | Format-Table -Property Name, Platform, DriverVersion -AutoSize
}

if ($Clean) {
    Write-Info "Removing cpplinq test DSNs..."
    foreach ($dsn in @("MSSQLLocalDB", "PostgreSQL35W")) {
        $existing = Get-OdbcDsn -Name $dsn -ErrorAction SilentlyContinue
        if ($existing) {
            Remove-OdbcDsn -Name $dsn -DsnType User -Platform 64-bit -ErrorAction SilentlyContinue
            Write-Success "Removed DSN: $dsn"
        } else {
            Write-Info "DSN $dsn does not exist."
        }
    }
    return
}

# 2. Find best matching SQL Server driver
Write-Info "Checking SQL Server ODBC drivers..."
$allDrivers = Get-OdbcDriver -Platform "64-bit" -ErrorAction SilentlyContinue
if (-not $allDrivers) {
    $allDrivers = Get-OdbcDriver -ErrorAction SilentlyContinue
}

$mssqlDriver = $null
$mssqlCandidates = @(
    "ODBC Driver 18 for SQL Server",
    "ODBC Driver 17 for SQL Server",
    "ODBC Driver 13 for SQL Server",
    "ODBC Driver 11 for SQL Server",
    "SQL Server Native Client 11.0",
    "SQL Server"
)

foreach ($c in $mssqlCandidates) {
    $found = $allDrivers | Where-Object { $_.Name -eq $c } | Select-Object -First 1
    if ($found) {
        $mssqlDriver = $found.Name
        break
    }
}

if ($mssqlDriver) {
    Write-Success "Found SQL Server ODBC Driver: $mssqlDriver"
    
    # Configure MSSQLLocalDB DSN
    $existingMssqlDsn = Get-OdbcDsn -Name "MSSQLLocalDB" -ErrorAction SilentlyContinue
    if ($existingMssqlDsn -and -not $Force) {
        Write-Info "DSN 'MSSQLLocalDB' already exists using driver '$($existingMssqlDsn.DriverName)'."
    } else {
        if ($existingMssqlDsn) {
            Remove-OdbcDsn -Name "MSSQLLocalDB" -DsnType User -Platform "64-bit" -ErrorAction SilentlyContinue
        }
        
        $setAttrs = @(
            "Server=(localdb)\MSSQLLocalDB",
            "Database=tempdb",
            "Trusted_Connection=Yes"
        )
        if ($mssqlDriver -match "ODBC Driver 18") {
            $setAttrs += "TrustServerCertificate=Yes"
        }
        
        Add-OdbcDsn -Name "MSSQLLocalDB" -DriverName $mssqlDriver -DsnType User -Platform "64-bit" `
            -SetPropertyValue $setAttrs
        Write-Success "Configured User DSN 'MSSQLLocalDB' -> $mssqlDriver"
    }
} else {
    Write-Warn "No SQL Server ODBC Driver found. Download 'ODBC Driver 18 for SQL Server' from Microsoft if needed."
}

# 3. Find best matching PostgreSQL driver
Write-Info "Checking PostgreSQL ODBC drivers..."
$pgDriver = $null
$pgCandidates = @(
    "PostgreSQL Unicode(x64)",
    "PostgreSQL Unicode",
    "PostgreSQL ANSI(x64)",
    "PostgreSQL ANSI"
)

foreach ($c in $pgCandidates) {
    $found = $allDrivers | Where-Object { $_.Name -eq $c } | Select-Object -First 1
    if ($found) {
        $pgDriver = $found.Name
        break
    }
}

if ($pgDriver) {
    Write-Success "Found PostgreSQL ODBC Driver: $pgDriver"
    
    # Configure PostgreSQL35W DSN
    $existingPgDsn = Get-OdbcDsn -Name "PostgreSQL35W" -ErrorAction SilentlyContinue
    if ($existingPgDsn -and -not $Force) {
        Write-Info "DSN 'PostgreSQL35W' already exists using driver '$($existingPgDsn.DriverName)'."
    } else {
        if ($existingPgDsn) {
            Remove-OdbcDsn -Name "PostgreSQL35W" -DsnType User -Platform "64-bit" -ErrorAction SilentlyContinue
        }
        
        Add-OdbcDsn -Name "PostgreSQL35W" -DriverName $pgDriver -DsnType User -Platform "64-bit" `
            -SetPropertyValue @(
                "Server=localhost",
                "Port=5432",
                "Database=postgres",
                "Username=postgres",
                "Password=postgres"
            )
        Write-Success "Configured User DSN 'PostgreSQL35W' -> $pgDriver"
    }
} else {
    Write-Warn "No PostgreSQL ODBC Driver found. Download 'psqlodbc' x64 installer from postgresql.org if needed."
}

Write-Info ""
Write-Info "Configured ODBC DSNs:"
Get-OdbcDsn -DsnType User | Where-Object { $_.Name -in @("MSSQLLocalDB", "PostgreSQL35W") } | Format-Table -Property Name, DriverName, Platform -AutoSize
