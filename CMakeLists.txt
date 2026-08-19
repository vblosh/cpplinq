cmake_minimum_required(VERSION 3.20)
project(cpplinq VERSION 0.1.0 LANGUAGES C CXX)

# Disable vcpkg MSBuild integration so FetchContent GoogleTest is used purely and statically
set(CMAKE_VS_GLOBALS "VcpkgEnabled=false;VcpkgAutoLink=false;VcpkgApplocalDeps=false")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Options
option(CPPLINQ_BUILD_TESTS    "Build unit tests"    ON)
option(CPPLINQ_BUILD_EXAMPLES "Build examples"      ON)
option(CPPLINQ_ENABLE_SQLITE  "Enable SQLite backend"     ON)
option(CPPLINQ_ENABLE_POSTGRES "Enable PostgreSQL backend (ODBC)" ON)
option(CPPLINQ_ENABLE_MSSQL    "Enable Microsoft SQL Server backend" ON)
option(CPPLINQ_ENABLE_MYSQL    "Enable MySQL / MariaDB backend" ON)
option(CPPLINQ_ENABLE_INFORMIX "Enable IBM Informix backend (ODBC)" ON)
option(CPPLINQ_BUILD_BENCHMARKS "Build performance benchmarks" OFF)

# SQLite3: download amalgamation and build from source
if(CPPLINQ_ENABLE_SQLITE)
    FetchContent_Declare(
        sqlite3
        URL https://www.sqlite.org/2024/sqlite-amalgamation-3450100.zip
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(sqlite3)
    if(NOT TARGET SQLite::SQLite3)
        add_library(sqlite3_lib STATIC ${sqlite3_SOURCE_DIR}/sqlite3.c)
        target_include_directories(sqlite3_lib PUBLIC ${sqlite3_SOURCE_DIR})
        target_compile_definitions(sqlite3_lib PRIVATE
            SQLITE_ENABLE_COLUMN_METADATA
            SQLITE_ENABLE_FTS5
            SQLITE_ENABLE_JSON1
        )
        add_library(SQLite::SQLite3 ALIAS sqlite3_lib)
    endif()
endif()

# Library
add_subdirectory(src)

# Tests
if(CPPLINQ_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Examples
if(CPPLINQ_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Benchmarks
if(CPPLINQ_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()
