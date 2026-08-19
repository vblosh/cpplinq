<#
.SYNOPSIS
    Builds, configures ODBC sources, and runs integration tests for cpplinq backends (MSSQL, PostgreSQL, MySQL, Informix, SQLite).

.DESCRIPTION
    This script automates integration testing with live databases and ODBC sources for cpplinq:
    1. Detects installed ODBC drivers (SQL Server, PostgreSQL, MySQL, Informix, SQLite).
    2. Starts local database engines (LocalDB / local services or Docker containers).
    3. Exports ODBC connection strings (CPPLINQ_MSSQL_ODBC, CPPLINQ_POSTGRES_ODBC, CPPLINQ_MYSQL_ODBC, CPPLINQ_INFORMIX_ODBC, CPPLINQ_SQLITE_ODBC).
    4. Auto-locates CMake and builds integration test targets with all backend options enabled.
    5. Executes selected integration test suites and displays a detailed report.

.PARAMETER Database
    Which database tests to run: "All", "MSSQL", "PostgreSQL", "MySQL", "Informix", "SQLite". Default is "All".

.PARAMETER Mode
    Database host mode: "Auto", "Local", or "Docker".
    - "Auto": Uses local services/LocalDB if available; falls back to Docker.
    - "Local": Uses Windows SQL Server LocalDB and local database services.
    - "Docker": Starts test database containers via docker-compose.integration.yml.

.PARAMETER BuildType
    CMake build configuration: "Release" (default), "Debug", "RelWithDebInfo".

.PARAMETER BuildDir
    The build directory path relative to project root. Default is "build".

.PARAMETER SkipBuild
    Skips the CMake configure and build steps, running existing test executables.

.PARAMETER MssqlConn
    Overrides the Microsoft SQL Server ODBC connection string.

.PARAMETER PostgresConn
    Overrides the PostgreSQL ODBC connection string.

.PARAMETER MysqlConn
    Overrides the MySQL / MariaDB ODBC connection string.

.PARAMETER InformixConn
    Overrides the IBM Informix ODBC connection string.

.PARAMETER SqliteConn
    Overrides the SQLite ODBC connection string.

.PARAMETER StopDockerOnExit
    If containers were started by this script, stops them when finished.

.EXAMPLE
    .\scripts\run_odbc_integration_tests.ps1
    .\scripts\run_odbc_integration_tests.ps1 -Database MSSQL
    .\scripts\run_odbc_integration_tests.ps1 -Database PostgreSQL
    .\scripts\run_odbc_integration_tests.ps1 -Database MySQL
    .\scripts\run_odbc_integration_tests.ps1 -Database Informix
    .\scripts\run_odbc_integration_tests.ps1 -Mode Docker -StopDockerOnExit
#>

[CmdletBinding()]
param(
    [ValidateSet("All", "MSSQL", "PostgreSQL", "MySQL", "Informix", "SQLite")]
    [string]$Database = "All",

    [ValidateSet("Auto", "Local", "Docker")]
    [string]$Mode = "Auto",

    [ValidateSet("Release", "Debug", "RelWithDebInfo")]
    [string]$BuildType = "Release",

    [string]$BuildDir = "build",

    [switch]$SkipBuild,

    [string]$MssqlConn = "",

    [string]$PostgresConn = "",

    [string]$MysqlConn = "",

    [string]$InformixConn = "",

    [string]$SqliteConn = "",

    [switch]$StopDockerOnExit
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$ComposeFile = Join-Path $PSScriptRoot "docker-compose.integration.yml"
$DockerStartedServices = [System.Collections.Generic.List[string]]::new()

function Write-Step([string]$msg) {
    Write-Host "`n========================================================" -ForegroundColor Cyan
    Write-Host ">> $msg" -ForegroundColor Cyan
    Write-Host "========================================================" -ForegroundColor Cyan
}

function Write-Info([string]$msg) {
    Write-Host "[INFO] $msg" -ForegroundColor Gray
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

function Import-VsEnvironment() {
    if (Get-Command "cl.exe" -ErrorAction SilentlyContinue) {
        return
    }

    $vcvars = $null
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($vsPath) {
            $candidate = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"
            if (Test-Path $candidate) {
                $vcvars = $candidate
            }
        }
    }

    if (-not $vcvars) {
        $candidates = @(
            "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat",
            "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat",
            "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat",
            "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
        )
        foreach ($c in $candidates) {
            if (Test-Path $c) {
                $vcvars = $c
                break
            }
        }
    }

    if ($vcvars) {
        Write-Info "Importing Visual Studio C++ environment from $vcvars..."
        cmd.exe /c "call `"$vcvars`" >nul && set" | ForEach-Object {
            if ($_ -match '^(.*?)=(.*)$') {
                [System.Environment]::SetEnvironmentVariable($matches[1], $matches[2], [System.EnvironmentVariableTarget]::Process)
            }
        }
    }
}

function Find-CMake() {
    $cmakeCmd = Get-Command "cmake" -ErrorAction SilentlyContinue
    if ($cmakeCmd) {
        return $cmakeCmd.Source
    }

    $searchPaths = @(
        "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
        "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
        "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
        "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
        "C:\Program Files\CMake\bin\cmake.exe"
    )

    foreach ($p in $searchPaths) {
        if (Test-Path $p) {
            return $p
        }
    }

    return $null
}

function Test-PortOpen([string]$hostName, [int]$port) {
    try {
        $client = New-Object System.Net.Sockets.TcpClient
        $iar = $client.BeginConnect($hostName, $port, $null, $null)
        $wait = $iar.AsyncWaitHandle.WaitOne(1500, $false)
        if ($wait) {
            $client.EndConnect($iar)
            $client.Close()
            return $true
        }
        $client.Close()
        return $false
    } catch {
        return $false
    }
}

function Find-OdbcDriver([string[]]$candidates) {
    $drivers = Get-OdbcDriver -Platform "64-bit" -ErrorAction SilentlyContinue
    if (-not $drivers) {
        $drivers = Get-OdbcDriver -ErrorAction SilentlyContinue
    }
    foreach ($cand in $candidates) {
        $match = $drivers | Where-Object { $_.Name -like "*$cand*" -or $_.Name -eq $cand } | Select-Object -First 1
        if ($match) {
            return $match.Name
        }
    }
    return $null
}

function Invoke-DockerComposeUp([string]$serviceName) {
    $docker = Get-Command "docker" -ErrorAction SilentlyContinue
    if ($docker) {
        docker compose -f $ComposeFile up -d $serviceName
        $DockerStartedServices.Add($serviceName)
        return $true
    }
    return $false
}

# -----------------------------------------------------------------------------
# 1. Environment & Driver Verification
# -----------------------------------------------------------------------------
Write-Step "Checking ODBC Drivers & Database Environment"

$mssqlDriver = Find-OdbcDriver @(
    "ODBC Driver 18 for SQL Server",
    "ODBC Driver 17 for SQL Server",
    "ODBC Driver 13 for SQL Server",
    "ODBC Driver 11 for SQL Server",
    "SQL Server Native Client 11.0",
    "SQL Server"
)

$postgresDriver = Find-OdbcDriver @(
    "PostgreSQL Unicode(x64)",
    "PostgreSQL Unicode",
    "PostgreSQL ANSI(x64)",
    "PostgreSQL ANSI"
)

$mysqlDriver = Find-OdbcDriver @(
    "MySQL ODBC 8.0 Unicode Driver",
    "MySQL ODBC 8.0 ANSI Driver",
    "MySQL ODBC 9.0 Unicode Driver",
    "MySQL ODBC 9.0 ANSI Driver",
    "MariaDB Unicode",
    "MariaDB ODBC 3.1 Driver",
    "MySQL ODBC 5.3 Unicode Driver"
)

$informixDriver = Find-OdbcDriver @(
    "IBM INFORMIX ODBC DRIVER (64-bit)",
    "IBM INFORMIX ODBC DRIVER",
    "IBM INFORMIX 3.82 32 BIT"
)

$sqliteDriver = Find-OdbcDriver @(
    "SQLite3 ODBC Driver",
    "SQLite ODBC Driver"
)

Write-Info "Detected SQL Server ODBC Driver : $(if ($mssqlDriver) { $mssqlDriver } else { 'None' })"
Write-Info "Detected PostgreSQL ODBC Driver : $(if ($postgresDriver) { $postgresDriver } else { 'None' })"
Write-Info "Detected MySQL ODBC Driver      : $(if ($mysqlDriver) { $mysqlDriver } else { 'None' })"
Write-Info "Detected Informix ODBC Driver   : $(if ($informixDriver) { $informixDriver } else { 'None' })"
Write-Info "Detected SQLite ODBC Driver     : $(if ($sqliteDriver) { $sqliteDriver } else { 'None' })"

# -----------------------------------------------------------------------------
# 2. Setup Microsoft SQL Server
# -----------------------------------------------------------------------------
if ($Database -in @("All", "MSSQL")) {
    Write-Step "Setting up Microsoft SQL Server Source"

    if ($MssqlConn) {
        $env:CPPLINQ_MSSQL_ODBC = $MssqlConn
        $env:CPPDB_MSSQL_ODBC = $MssqlConn
        Write-Success "Using custom MSSQL connection string: $env:CPPLINQ_MSSQL_ODBC"
    } else {
        $driver = if ($mssqlDriver) { $mssqlDriver } else { "ODBC Driver 18 for SQL Server" }
        $mssqlConfigured = $false

        # Check LocalDB or local port 1433 if mode is Local or Auto
        if ($Mode -in @("Auto", "Local")) {
            $sqllocaldb = Get-Command "sqllocaldb" -ErrorAction SilentlyContinue
            if ($sqllocaldb) {
                Write-Info "Starting SQL Server LocalDB (MSSQLLocalDB)..."
                try {
                    & sqllocaldb start MSSQLLocalDB | Out-Null
                    $trustCert = if ($driver -match "ODBC Driver 18") { "TrustServerCertificate=yes;" } else { "" }
                    $env:CPPLINQ_MSSQL_ODBC = "Driver={$driver};Server=(localdb)\MSSQLLocalDB;Database=tempdb;Trusted_Connection=yes;$trustCert"
                    $env:CPPDB_MSSQL_ODBC = $env:CPPLINQ_MSSQL_ODBC
                    Write-Success "SQL Server LocalDB is ready. Connection: $env:CPPLINQ_MSSQL_ODBC"
                    $mssqlConfigured = $true
                } catch {
                    Write-Warn "Could not start MSSQLLocalDB: $_"
                }
            } elseif (Test-PortOpen "127.0.0.1" 1433) {
                Write-Info "Detected local SQL Server on port 1433."
                $trustCert = if ($driver -match "ODBC Driver 18") { "TrustServerCertificate=yes;" } else { "" }
                $env:CPPLINQ_MSSQL_ODBC = "Driver={$driver};Server=127.0.0.1,1433;Database=master;Uid=sa;Pwd=Password123!;$trustCert"
                $env:CPPDB_MSSQL_ODBC = $env:CPPLINQ_MSSQL_ODBC
                Write-Success "SQL Server service is ready. Connection: $env:CPPLINQ_MSSQL_ODBC"
                $mssqlConfigured = $true
            }
        }

        # If not configured and mode is Docker or Auto fallback
        if (-not $mssqlConfigured -and $Mode -in @("Auto", "Docker")) {
            if (Invoke-DockerComposeUp "mssql") {
                Write-Info "Waiting for MSSQL container on port 1433..."
                $ready = $false
                for ($i = 0; $i -lt 30; $i++) {
                    if (Test-PortOpen "127.0.0.1" 1433) {
                        $ready = $true
                        break
                    }
                    Start-Sleep -Seconds 1
                }

                if ($ready) {
                    $trustCert = if ($driver -match "ODBC Driver 18") { "TrustServerCertificate=yes;" } else { "" }
                    $env:CPPLINQ_MSSQL_ODBC = "Driver={$driver};Server=127.0.0.1,1433;Database=master;Uid=sa;Pwd=Password123!;$trustCert"
                    $env:CPPDB_MSSQL_ODBC = $env:CPPLINQ_MSSQL_ODBC
                    Write-Success "MSSQL Docker container is ready. Connection: $env:CPPLINQ_MSSQL_ODBC"
                    $mssqlConfigured = $true
                } else {
                    Write-Warn "Timed out waiting for MSSQL container port 1433."
                }
            } else {
                Write-Warn "Docker not found; could not spin up MSSQL container."
            }
        }

        if (-not $mssqlConfigured) {
            Write-Warn "No local or Docker MSSQL service configured. Tests may be skipped."
        }
    }
}

# -----------------------------------------------------------------------------
# 3. Setup PostgreSQL
# -----------------------------------------------------------------------------
if ($Database -in @("All", "PostgreSQL")) {
    Write-Step "Setting up PostgreSQL Source"

    if ($PostgresConn) {
        $env:CPPLINQ_POSTGRES_ODBC = $PostgresConn
        $env:CPPDB_POSTGRES_ODBC = $PostgresConn
        Write-Success "Using custom PostgreSQL connection string: $env:CPPLINQ_POSTGRES_ODBC"
    } else {
        $driver = if ($postgresDriver) { $postgresDriver } else { "PostgreSQL Unicode(x64)" }
        $pgConfigured = $false

        # Check local port 5432 or DSN if mode is Local or Auto
        if ($Mode -in @("Auto", "Local")) {
            # If docker container is running, ensure cppdb user and database are provisioned
            try {
                & docker exec cpplinq-postgres-test psql -U cppdb -d cppdb -c "DO `$psql`$ BEGIN IF NOT EXISTS (SELECT FROM pg_catalog.pg_roles WHERE rolname = 'cppdb') THEN CREATE ROLE cppdb WITH SUPERUSER LOGIN PASSWORD 'cppdb_password'; ELSE ALTER ROLE cppdb WITH SUPERUSER LOGIN PASSWORD 'cppdb_password'; END IF; END `$psql`$;" -c "SELECT 'CREATE DATABASE cppdb OWNER cppdb' WHERE NOT EXISTS (SELECT FROM pg_database WHERE datname = 'cppdb')\gexec" -c "GRANT ALL PRIVILEGES ON DATABASE cppdb TO cppdb;" 2>$null | Out-Null
            } catch {}

            $existingDsn = Get-OdbcDsn -Name "PostgreSQL35W" -ErrorAction SilentlyContinue
            if ($existingDsn) {
                $env:CPPLINQ_POSTGRES_ODBC = "DSN=PostgreSQL35W;"
                $env:CPPDB_POSTGRES_ODBC = $env:CPPLINQ_POSTGRES_ODBC
                Write-Success "PostgreSQL configured using DSN: $env:CPPLINQ_POSTGRES_ODBC"
                $pgConfigured = $true
            } elseif (Test-PortOpen "localhost" 5432) {
                Write-Info "Detected local PostgreSQL instance on port 5432."
                $env:CPPLINQ_POSTGRES_ODBC = "Driver={$driver};Server=localhost;Port=5432;Database=cppdb;Uid=cppdb;Pwd=cppdb_password;"
                $env:CPPDB_POSTGRES_ODBC = $env:CPPLINQ_POSTGRES_ODBC
                Write-Success "PostgreSQL service is ready. Connection: $env:CPPLINQ_POSTGRES_ODBC"
                $pgConfigured = $true
            }
        }

        # If not configured and mode is Docker or Auto fallback
        if (-not $pgConfigured -and $Mode -in @("Auto", "Docker")) {
            if (Invoke-DockerComposeUp "postgres") {
                Write-Info "Waiting for PostgreSQL container on port 5432..."
                $ready = $false
                for ($i = 0; $i -lt 30; $i++) {
                    if (Test-PortOpen "127.0.0.1" 5432) {
                        $ready = $true
                        break
                    }
                    Start-Sleep -Seconds 1
                }

                if ($ready) {
                    Start-Sleep -Seconds 2
                    try {
                        & docker exec cpplinq-postgres-test psql -U cppdb -d cppdb -c "DO `$psql`$ BEGIN IF NOT EXISTS (SELECT FROM pg_catalog.pg_roles WHERE rolname = 'cppdb') THEN CREATE ROLE cppdb WITH SUPERUSER LOGIN PASSWORD 'cppdb_password'; ELSE ALTER ROLE cppdb WITH SUPERUSER LOGIN PASSWORD 'cppdb_password'; END IF; END `$psql`$;" -c "SELECT 'CREATE DATABASE cppdb OWNER cppdb' WHERE NOT EXISTS (SELECT FROM pg_database WHERE datname = 'cppdb')\gexec" -c "GRANT ALL PRIVILEGES ON DATABASE cppdb TO cppdb;" 2>$null | Out-Null
                    } catch {
                        Write-Warn "Could not auto-provision PostgreSQL cppdb: $_"
                    }

                    $env:CPPLINQ_POSTGRES_ODBC = "Driver={$driver};Server=127.0.0.1;Port=5432;Database=cppdb;Uid=cppdb;Pwd=cppdb_password;"
                    $env:CPPDB_POSTGRES_ODBC = $env:CPPLINQ_POSTGRES_ODBC
                    Write-Success "PostgreSQL Docker container is ready. Connection: $env:CPPLINQ_POSTGRES_ODBC"
                    $pgConfigured = $true
                } else {
                    Write-Warn "Timed out waiting for PostgreSQL container port 5432."
                }
            } else {
                Write-Warn "Docker not found; could not spin up PostgreSQL container."
            }
        }

        if (-not $pgConfigured) {
            Write-Warn "No local or Docker PostgreSQL service configured. Tests may be skipped."
        }
    }
}

# -----------------------------------------------------------------------------
# 4. Setup MySQL / MariaDB
# -----------------------------------------------------------------------------
if ($Database -in @("All", "MySQL")) {
    Write-Step "Setting up MySQL / MariaDB Source"

    if ($MysqlConn) {
        $env:CPPLINQ_MYSQL_ODBC = $MysqlConn
        $env:CPPDB_MYSQL_ODBC = $MysqlConn
        Write-Success "Using custom MySQL connection string: $env:CPPLINQ_MYSQL_ODBC"
    } else {
        $driver = if ($mysqlDriver) { $mysqlDriver } else { "MySQL ODBC 8.0 Unicode Driver" }
        $mysqlConfigured = $false

        # Check local DSN or local port 3306 if mode is Local or Auto
        if ($Mode -in @("Auto", "Local")) {
            # If docker container is running, ensure testuser/testdb are provisioned for DSN compatibility
            try {
                & docker exec cpplinq-mysql-test mysql -uroot -proot_password -e "CREATE DATABASE IF NOT EXISTS testdb; CREATE USER IF NOT EXISTS 'testuser'@'%' IDENTIFIED BY 'testpass'; ALTER USER 'testuser'@'%' IDENTIFIED BY 'testpass'; GRANT ALL PRIVILEGES ON *.* TO 'testuser'@'%' WITH GRANT OPTION; GRANT ALL PRIVILEGES ON *.* TO 'cppdb'@'%' WITH GRANT OPTION; FLUSH PRIVILEGES;" 2>$null | Out-Null
            } catch {}

            $existingDsn = Get-OdbcDsn -Name "MySQLtestdb" -ErrorAction SilentlyContinue
            if ($existingDsn) {
                $env:CPPLINQ_MYSQL_ODBC = "DSN=MySQLtestdb;"
                $env:CPPDB_MYSQL_ODBC = $env:CPPLINQ_MYSQL_ODBC
                Write-Success "MySQL configured using DSN: $env:CPPLINQ_MYSQL_ODBC"
                $mysqlConfigured = $true
            } elseif (Test-PortOpen "localhost" 3306) {
                Write-Info "Detected local MySQL instance on port 3306."
                $env:CPPLINQ_MYSQL_ODBC = "Driver={$driver};Server=127.0.0.1;Port=3306;Database=cppdb;Uid=cppdb;Pwd=cppdb_password;"
                $env:CPPDB_MYSQL_ODBC = $env:CPPLINQ_MYSQL_ODBC
                Write-Success "MySQL service is ready. Connection: $env:CPPLINQ_MYSQL_ODBC"
                $mysqlConfigured = $true
            }
        }

        # If not configured and mode is Docker or Auto fallback
        if (-not $mysqlConfigured -and $Mode -in @("Auto", "Docker")) {
            if (Invoke-DockerComposeUp "mysql") {
                Write-Info "Waiting for MySQL container on port 3306..."
                $ready = $false
                for ($i = 0; $i -lt 30; $i++) {
                    if (Test-PortOpen "127.0.0.1" 3306) {
                        $ready = $true
                        break
                    }
                    Start-Sleep -Seconds 1
                }

                if ($ready) {
                    Start-Sleep -Seconds 2
                    try {
                        & docker exec cpplinq-mysql-test mysql -uroot -proot_password -e "CREATE DATABASE IF NOT EXISTS testdb; CREATE USER IF NOT EXISTS 'testuser'@'%' IDENTIFIED BY 'testpass'; ALTER USER 'testuser'@'%' IDENTIFIED BY 'testpass'; GRANT ALL PRIVILEGES ON *.* TO 'testuser'@'%' WITH GRANT OPTION; GRANT ALL PRIVILEGES ON *.* TO 'cppdb'@'%' WITH GRANT OPTION; FLUSH PRIVILEGES;" 2>$null | Out-Null
                    } catch {
                        Write-Warn "Could not auto-provision MySQL testdb/testuser: $_"
                    }

                    $env:CPPLINQ_MYSQL_ODBC = "Driver={$driver};Server=127.0.0.1;Port=3306;Database=cppdb;Uid=cppdb;Pwd=cppdb_password;"
                    $env:CPPDB_MYSQL_ODBC = $env:CPPLINQ_MYSQL_ODBC
                    Write-Success "MySQL Docker container is ready. Connection: $env:CPPLINQ_MYSQL_ODBC"
                    $mysqlConfigured = $true
                } else {
                    Write-Warn "Timed out waiting for MySQL container port 3306."
                }
            } else {
                Write-Warn "Docker not found; could not spin up MySQL container."
            }
        }

        if (-not $mysqlConfigured) {
            Write-Warn "No local or Docker MySQL service configured. Tests may be skipped."
        }
    }
}

# -----------------------------------------------------------------------------
# 5. Setup IBM Informix
# -----------------------------------------------------------------------------
if ($Database -in @("All", "Informix")) {
    Write-Step "Setting up IBM Informix Source"

    if ($InformixConn) {
        $env:CPPLINQ_INFORMIX_ODBC = $InformixConn
        $env:CPPDB_INFORMIX_ODBC = $InformixConn
        Write-Success "Using custom Informix connection string: $env:CPPLINQ_INFORMIX_ODBC"
    } else {
        $driver = if ($informixDriver) { $informixDriver } else { "IBM INFORMIX ODBC DRIVER (64-bit)" }
        $informixConfigured = $false

        # Check local DSN or local port 9088 if mode is Local or Auto
        if ($Mode -in @("Auto", "Local")) {
            $existingDsn = Get-OdbcDsn -Name "InformixDSN" -ErrorAction SilentlyContinue
            if ($existingDsn) {
                $env:CPPLINQ_INFORMIX_ODBC = "DSN=InformixDSN;"
                $env:CPPDB_INFORMIX_ODBC = $env:CPPLINQ_INFORMIX_ODBC
                Write-Success "Informix configured using DSN: $env:CPPLINQ_INFORMIX_ODBC"
                $informixConfigured = $true
            } elseif (Test-PortOpen "localhost" 9088) {
                Write-Info "Detected Informix instance on port 9088."
                $env:CPPLINQ_INFORMIX_ODBC = "Driver={$driver};Server=informix;Database=testdb;Host=127.0.0.1;Service=9088;Uid=informix;Pwd=in4mix;"
                $env:CPPDB_INFORMIX_ODBC = $env:CPPLINQ_INFORMIX_ODBC
                Write-Success "Informix service is ready. Connection: $env:CPPLINQ_INFORMIX_ODBC"
                $informixConfigured = $true
            }
        }

        # If not configured and mode is Docker or Auto fallback
        if (-not $informixConfigured -and $Mode -in @("Auto", "Docker")) {
            if (Invoke-DockerComposeUp "informix") {
                Write-Info "Waiting for Informix container on port 9088..."
                $ready = $false
                for ($i = 0; $i -lt 40; $i++) {
                    if (Test-PortOpen "127.0.0.1" 9088) {
                        $ready = $true
                        break
                    }
                    Start-Sleep -Seconds 1
                }

                if ($ready) {
                    # Initialize testdb database with logging inside Informix container
                    Start-Sleep -Seconds 2
                    try {
                        & docker exec cpplinq-informix-test bash -c 'echo "CREATE DATABASE testdb WITH LOG;" | dbaccess - - 2>/dev/null || true' | Out-Null
                    } catch {
                        Write-Warn "Could not auto-initialize Informix testdb: $_"
                    }

                    $env:CPPLINQ_INFORMIX_ODBC = "Driver={$driver};Server=informix;Database=testdb;Host=127.0.0.1;Service=9088;Uid=informix;Pwd=in4mix;"
                    $env:CPPDB_INFORMIX_ODBC = $env:CPPLINQ_INFORMIX_ODBC
                    Write-Success "Informix Docker container is ready. Connection: $env:CPPLINQ_INFORMIX_ODBC"
                    $informixConfigured = $true
                } else {
                    Write-Warn "Timed out waiting for Informix container port 9088."
                }
            } else {
                Write-Warn "Docker not found; could not spin up Informix container."
            }
        }

        if (-not $informixConfigured) {
            Write-Warn "No local or Docker Informix service configured. Tests may be skipped."
        }
    }
}

# -----------------------------------------------------------------------------
# 6. Setup SQLite
# -----------------------------------------------------------------------------
if ($Database -in @("All", "SQLite")) {
    if ($SqliteConn) {
        $env:CPPLINQ_SQLITE_ODBC = $SqliteConn
        $env:CPPDB_SQLITE_ODBC = $SqliteConn
    } elseif ($sqliteDriver) {
        $tempDb = Join-Path $env:TEMP "cppdb.sqlite"
        $env:CPPLINQ_SQLITE_ODBC = "Driver={$sqliteDriver};Database=$tempDb;"
        $env:CPPDB_SQLITE_ODBC = $env:CPPLINQ_SQLITE_ODBC
    }
}

# -----------------------------------------------------------------------------
# 7. Build Test Binaries with CMake
# -----------------------------------------------------------------------------
$fullBuildDir = Join-Path $ProjectRoot $BuildDir

if (-not $SkipBuild) {
    Write-Step "Configuring & Building Integration Tests"

    Import-VsEnvironment

    $cmake = Find-CMake
    if (-not $cmake) {
        throw "CMake executable not found. Please install CMake or run from a Visual Studio Developer Command Prompt."
    }
    Write-Info "Using CMake: $cmake"

    $targets = [System.Collections.Generic.List[string]]::new()
    if ($Database -in @("All", "SQLite")) {
        $targets.Add("test_sqlite_integration")
        $targets.Add("test_connection_pool")
        $targets.Add("test_streaming")
    }
    if ($Database -in @("All", "MSSQL")) {
        $targets.Add("test_mssql_integration")
    }
    if ($Database -in @("All", "PostgreSQL")) {
        $targets.Add("test_postgres_integration")
    }
    if ($Database -in @("All", "MySQL")) {
        $targets.Add("test_mysql_integration")
    }
    if ($Database -in @("All", "Informix")) {
        $targets.Add("test_informix_query_builder")
        $targets.Add("test_informix_integration")
    }

    # Configure if cache does not exist
    $cacheFile = Join-Path $fullBuildDir "CMakeCache.txt"
    if (-not (Test-Path $cacheFile)) {
        Write-Info "Configuring CMake project in $BuildDir..."
        & $cmake -S $ProjectRoot -B $fullBuildDir `
            -DCPPLINQ_BUILD_TESTS=ON `
            -DCPPLINQ_ENABLE_SQLITE=ON `
            -DCPPLINQ_ENABLE_MSSQL=ON `
            -DCPPLINQ_ENABLE_POSTGRES=ON `
            -DCPPLINQ_ENABLE_MYSQL=ON `
            -DCPPLINQ_ENABLE_INFORMIX=ON
        if ($LASTEXITCODE -ne 0) {
            throw "CMake configuration failed."
        }
    }

    $targetArgs = [System.Collections.Generic.List[string]]::new()
    foreach ($t in $targets) {
        $targetArgs.Add("--target")
        $targetArgs.Add($t)
    }

    # Build targets
    Write-Info "Building targets: $($targets -join ', ') [$BuildType]..."
    & $cmake --build $fullBuildDir --config $BuildType $targetArgs --parallel
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed."
    }
    Write-Success "Build completed successfully."
}

# -----------------------------------------------------------------------------
# 8. Execute Tests
# -----------------------------------------------------------------------------
Write-Step "Executing Integration Tests"

$testResults = [System.Collections.Generic.List[PSCustomObject]]::new()
$allPassed = $true

function Run-SingleTest([string]$exeName, [string]$label) {
    $candidatePaths = @(
        (Join-Path $fullBuildDir "tests\$BuildType\$exeName.exe"),
        (Join-Path $fullBuildDir "tests\$exeName.exe"),
        (Join-Path $fullBuildDir "$BuildType\$exeName.exe"),
        (Join-Path $fullBuildDir "$exeName.exe"),
        (Join-Path $fullBuildDir "bin\$BuildType\$exeName.exe"),
        (Join-Path $fullBuildDir "bin\$exeName.exe")
    )

    $exePath = $null
    foreach ($p in $candidatePaths) {
        if (Test-Path $p) {
            $exePath = $p
            break
        }
    }

    if (-not $exePath) {
        Write-Err "Could not find executable: $exeName.exe"
        $script:allPassed = $false
        $script:testResults.Add([PSCustomObject]@{
            Suite = $label
            Status = "MISSING_EXE"
            DurationSec = 0
        })
        return
    }

    Write-Host "`n>> Running $label ($exePath)..." -ForegroundColor Magenta
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    & $exePath
    $exitCode = $LASTEXITCODE
    $sw.Stop()

    $duration = [Math]::Round($sw.Elapsed.TotalSeconds, 2)
    $status = if ($exitCode -eq 0) { "PASSED" } else { "FAILED" }
    if ($exitCode -ne 0) {
        $script:allPassed = $false
    }

    $script:testResults.Add([PSCustomObject]@{
        Suite = $label
        Status = $status
        DurationSec = $duration
    })
}

try {
    if ($Database -in @("All", "SQLite")) {
        Run-SingleTest "test_sqlite_integration" "SQLite Integration Tests"
        Run-SingleTest "test_connection_pool" "Connection Pool Tests"
        Run-SingleTest "test_streaming" "Streaming Query Tests"
    }

    if ($Database -in @("All", "MSSQL")) {
        Run-SingleTest "test_mssql_integration" "MSSQL Integration Tests"
    }

    if ($Database -in @("All", "PostgreSQL")) {
        Run-SingleTest "test_postgres_integration" "PostgreSQL Integration Tests"
    }

    if ($Database -in @("All", "MySQL")) {
        Run-SingleTest "test_mysql_integration" "MySQL / MariaDB Integration Tests"
    }

    if ($Database -in @("All", "Informix")) {
        Run-SingleTest "test_informix_query_builder" "Informix Query Builder Tests"
        Run-SingleTest "test_informix_integration" "IBM Informix Integration Tests"
    }
}
finally {
    # Clean up docker services if requested
    if ($StopDockerOnExit -and $DockerStartedServices.Count -gt 0) {
        Write-Step "Cleaning up Docker Containers"
        docker compose -f $ComposeFile down
        Write-Success "Stopped Docker test services."
    }
}

# -----------------------------------------------------------------------------
# 9. Test Summary
# -----------------------------------------------------------------------------
Write-Step "Integration Test Summary"
$testResults | Format-Table -Property Suite, Status, DurationSec -AutoSize

if ($allPassed) {
    Write-Success "ALL INTEGRATION TESTS PASSED!"
    exit 0
} else {
    Write-Err "SOME INTEGRATION TESTS FAILED!"
    exit 1
}
