<#
.SYNOPSIS
    Builds, configures ODBC sources, and runs integration tests for Microsoft SQL Server and PostgreSQL.

.DESCRIPTION
    This script automates integration testing with ODBC sources for cpplinq:
    1. Detects installed ODBC drivers (SQL Server & PostgreSQL).
    2. Starts local database engines (SQL Server LocalDB / PostgreSQL service or Docker containers).
    3. Exports ODBC connection strings (CPPLINQ_MSSQL_ODBC, CPPLINQ_POSTGRES_ODBC).
    4. Auto-locates CMake and builds integration test targets with CPPLINQ_ENABLE_MSSQL=ON and CPPLINQ_ENABLE_POSTGRES=ON.
    5. Executes test_mssql_integration.exe and test_postgres_integration.exe and displays a detailed report.

.PARAMETER Database
    Which database tests to run: "All", "MSSQL", or "PostgreSQL". Default is "All".

.PARAMETER Mode
    Database host mode: "Auto", "Local", or "Docker".
    - "Auto": Uses local services/LocalDB if available; falls back to Docker.
    - "Local": Uses Windows SQL Server LocalDB and local PostgreSQL service.
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

.PARAMETER StopDockerOnExit
    If containers were started by this script, stops them when finished.

.EXAMPLE
    .\scripts\run_odbc_integration_tests.ps1
    .\scripts\run_odbc_integration_tests.ps1 -Database MSSQL
    .\scripts\run_odbc_integration_tests.ps1 -Database PostgreSQL
    .\scripts\run_odbc_integration_tests.ps1 -Mode Docker -StopDockerOnExit
#>

[CmdletBinding()]
param(
    [ValidateSet("All", "MSSQL", "PostgreSQL")]
    [string]$Database = "All",

    [ValidateSet("Auto", "Local", "Docker")]
    [string]$Mode = "Auto",

    [ValidateSet("Release", "Debug", "RelWithDebInfo")]
    [string]$BuildType = "Release",

    [string]$BuildDir = "build",

    [switch]$SkipBuild,

    [string]$MssqlConn = "",

    [string]$PostgresConn = "",

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
        $match = $drivers | Where-Object { $_.Name -eq $cand } | Select-Object -First 1
        if ($match) {
            return $match.Name
        }
    }
    return $null
}

# -----------------------------------------------------------------------------
# 1. Environment & Driver Verification
# -----------------------------------------------------------------------------
Write-Step "Checking ODBC Drivers & Database Environment"

$mssqlDriver = Find-OdbcDriver @(
    "ODBC Driver 18 for SQL Server",
    "ODBC Driver 17 for SQL Server",
    "SQL Server Native Client 11.0",
    "SQL Server"
)

$postgresDriver = Find-OdbcDriver @(
    "PostgreSQL Unicode(x64)",
    "PostgreSQL Unicode",
    "PostgreSQL ANSI(x64)",
    "PostgreSQL ANSI"
)

Write-Info "Detected SQL Server ODBC Driver: $(if ($mssqlDriver) { $mssqlDriver } else { 'None' })"
Write-Info "Detected PostgreSQL ODBC Driver: $(if ($postgresDriver) { $postgresDriver } else { 'None' })"

# -----------------------------------------------------------------------------
# 2. Setup Microsoft SQL Server
# -----------------------------------------------------------------------------
if ($Database -in @("All", "MSSQL")) {
    Write-Step "Setting up Microsoft SQL Server Source"

    if ($MssqlConn) {
        $env:CPPLINQ_MSSQL_ODBC = $MssqlConn
        Write-Success "Using custom MSSQL connection string: $env:CPPLINQ_MSSQL_ODBC"
    } else {
        $driver = if ($mssqlDriver) { $mssqlDriver } else { "ODBC Driver 18 for SQL Server" }
        $mssqlConfigured = $false

        # Check LocalDB if mode is Local or Auto
        if ($Mode -in @("Auto", "Local")) {
            $sqllocaldb = Get-Command "sqllocaldb" -ErrorAction SilentlyContinue
            if ($sqllocaldb) {
                Write-Info "Starting SQL Server LocalDB (MSSQLLocalDB)..."
                try {
                    & sqllocaldb start MSSQLLocalDB | Out-Null
                    $trustCert = if ($driver -match "ODBC Driver 18") { "TrustServerCertificate=yes;" } else { "" }
                    $env:CPPLINQ_MSSQL_ODBC = "Driver={$driver};Server=(localdb)\MSSQLLocalDB;Database=tempdb;Trusted_Connection=yes;$trustCert"
                    Write-Success "SQL Server LocalDB is ready. Connection: $env:CPPLINQ_MSSQL_ODBC"
                    $mssqlConfigured = $true
                } catch {
                    Write-Warn "Could not start MSSQLLocalDB: $_"
                }
            }
        }

        # If not configured and mode is Docker or Auto fallback
        if (-not $mssqlConfigured -and $Mode -in @("Auto", "Docker")) {
            $docker = Get-Command "docker" -ErrorAction SilentlyContinue
            if ($docker) {
                Write-Info "Starting MSSQL container via Docker Compose..."
                docker compose -f $ComposeFile up -d mssql
                $DockerStartedServices.Add("mssql")

                Write-Info "Waiting for MSSQL to become ready on port 1433..."
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
        Write-Success "Using custom PostgreSQL connection string: $env:CPPLINQ_POSTGRES_ODBC"
    } else {
        $driver = if ($postgresDriver) { $postgresDriver } else { "PostgreSQL Unicode(x64)" }
        $pgConfigured = $false

        # Check local port 5432 if mode is Local or Auto
        if ($Mode -in @("Auto", "Local")) {
            $existingDsn = Get-OdbcDsn -Name "PostgreSQL35W" -ErrorAction SilentlyContinue
            if ($existingDsn) {
                $env:CPPLINQ_POSTGRES_ODBC = "DSN=PostgreSQL35W;"
                Write-Success "PostgreSQL configured using DSN: $env:CPPLINQ_POSTGRES_ODBC"
                $pgConfigured = $true
            } elseif (Test-PortOpen "localhost" 5432) {
                Write-Info "Detected local PostgreSQL instance on port 5432."
                $env:CPPLINQ_POSTGRES_ODBC = "Driver={$driver};Server=localhost;Port=5432;Database=postgres;Uid=postgres;Pwd=postgres;"
                Write-Success "PostgreSQL service is ready. Connection: $env:CPPLINQ_POSTGRES_ODBC"
                $pgConfigured = $true
            }
        }

        # If not configured and mode is Docker or Auto fallback
        if (-not $pgConfigured -and $Mode -in @("Auto", "Docker")) {
            $docker = Get-Command "docker" -ErrorAction SilentlyContinue
            if ($docker) {
                Write-Info "Starting PostgreSQL container via Docker Compose..."
                docker compose -f $ComposeFile up -d postgres
                $DockerStartedServices.Add("postgres")

                Write-Info "Waiting for PostgreSQL to become ready on port 5432..."
                $ready = $false
                for ($i = 0; $i -lt 30; $i++) {
                    if (Test-PortOpen "127.0.0.1" 5432) {
                        $ready = $true
                        break
                    }
                    Start-Sleep -Seconds 1
                }

                if ($ready) {
                    $env:CPPLINQ_POSTGRES_ODBC = "Driver={$driver};Server=127.0.0.1;Port=5432;Database=cppdb;Uid=cppdb;Pwd=cppdb_password;"
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
# 4. Build Test Binaries with CMake
# -----------------------------------------------------------------------------
$fullBuildDir = Join-Path $ProjectRoot $BuildDir

if (-not $SkipBuild) {
    Write-Step "Configuring & Building Integration Tests"

    $cmake = Find-CMake
    if (-not $cmake) {
        throw "CMake executable not found. Please install CMake or run from a Visual Studio Developer Command Prompt."
    }
    Write-Info "Using CMake: $cmake"

    $targets = [System.Collections.Generic.List[string]]::new()
    if ($Database -in @("All", "MSSQL")) {
        $targets.Add("test_mssql_integration")
    }
    if ($Database -in @("All", "PostgreSQL")) {
        $targets.Add("test_postgres_integration")
    }

    # Configure if cache does not exist
    $cacheFile = Join-Path $fullBuildDir "CMakeCache.txt"
    if (-not (Test-Path $cacheFile)) {
        Write-Info "Configuring CMake project in $BuildDir..."
        & $cmake -S $ProjectRoot -B $fullBuildDir `
            -DCPPLINQ_BUILD_TESTS=ON `
            -DCPPLINQ_ENABLE_SQLITE=ON `
            -DCPPLINQ_ENABLE_MSSQL=ON `
            -DCPPLINQ_ENABLE_POSTGRES=ON
        if ($LASTEXITCODE -ne 0) {
            throw "CMake configuration failed."
        }
    }

    # Build targets
    Write-Info "Building targets: $($targets -join ', ') [$BuildType]..."
    & $cmake --build $fullBuildDir --config $BuildType --target $targets --parallel
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed."
    }
    Write-Success "Build completed successfully."
}

# -----------------------------------------------------------------------------
# 5. Execute Tests
# -----------------------------------------------------------------------------
Write-Step "Executing ODBC Integration Tests"

$testResults = [System.Collections.Generic.List[PSCustomObject]]::new()
$allPassed = $true

function Run-SingleTest([string]$exeName, [string]$label) {
    # Find test executable in $fullBuildDir (could be in Release/, Debug/, tests/Release/, tests/, etc.)
    $candidatePaths = @(
        (Join-Path $fullBuildDir "tests\$BuildType\$exeName.exe"),
        (Join-Path $fullBuildDir "tests\$exeName.exe"),
        (Join-Path $fullBuildDir "$BuildType\$exeName.exe"),
        (Join-Path $fullBuildDir "$exeName.exe")
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
    if ($Database -in @("All", "MSSQL")) {
        Run-SingleTest "test_mssql_integration" "MSSQL Integration Tests"
    }

    if ($Database -in @("All", "PostgreSQL")) {
        Run-SingleTest "test_postgres_integration" "PostgreSQL Integration Tests"
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
# 6. Test Summary
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
