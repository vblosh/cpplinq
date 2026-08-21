set(CPPLINQ_SOURCES
    sql_generator.cpp
    driver/connection.cpp
)

if(CPPLINQ_ENABLE_MSSQL OR CPPLINQ_ENABLE_MYSQL OR CPPLINQ_ENABLE_INFORMIX OR CPPLINQ_ENABLE_ORACLE)
    list(APPEND CPPLINQ_SOURCES
        driver/odbc_connection.cpp
    )
endif()

if(CPPLINQ_ENABLE_SQLITE)
    list(APPEND CPPLINQ_SOURCES
        dialect/sqlite_dialect.cpp
        driver/sqlite_connection.cpp
    )
endif()

if(CPPLINQ_ENABLE_POSTGRES)
    list(APPEND CPPLINQ_SOURCES
        dialect/postgres_dialect.cpp
        driver/postgres_connection.cpp
    )
endif()

if(CPPLINQ_ENABLE_MSSQL)
    list(APPEND CPPLINQ_SOURCES
        dialect/mssql_dialect.cpp
        driver/mssql_connection.cpp
    )
endif()

if(CPPLINQ_ENABLE_MYSQL)
    list(APPEND CPPLINQ_SOURCES
        dialect/mysql_dialect.cpp
        driver/mysql_connection.cpp
    )
endif()

if(CPPLINQ_ENABLE_INFORMIX)
    list(APPEND CPPLINQ_SOURCES
        dialect/informix_dialect.cpp
        driver/informix_connection.cpp
    )
endif()

if(CPPLINQ_ENABLE_ORACLE)
    list(APPEND CPPLINQ_SOURCES
        dialect/oracle_dialect.cpp
        driver/oracle_connection.cpp
    )
endif()

add_library(cpplinq ${CPPLINQ_SOURCES})
target_include_directories(cpplinq PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_include_directories(cpplinq PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(cpplinq PUBLIC cxx_std_20)

if(CPPLINQ_ENABLE_SQLITE)
    target_link_libraries(cpplinq PUBLIC SQLite::SQLite3)
    target_compile_definitions(cpplinq PUBLIC CPPLINQ_HAS_SQLITE=1)
endif()

if(CPPLINQ_ENABLE_POSTGRES)
    find_package(PostgreSQL REQUIRED)
    target_include_directories(cpplinq PUBLIC ${PostgreSQL_INCLUDE_DIRS})
    target_link_libraries(cpplinq PUBLIC ${PostgreSQL_LIBRARIES})
    target_compile_definitions(cpplinq PUBLIC CPPLINQ_HAS_POSTGRES=1)
endif()

if(CPPLINQ_ENABLE_MSSQL)
    if(WIN32)
        target_link_libraries(cpplinq PUBLIC odbc32)
    else()
        find_package(ODBC REQUIRED)
        target_link_libraries(cpplinq PUBLIC ODBC::ODBC)
    endif()
    target_compile_definitions(cpplinq PUBLIC CPPLINQ_HAS_MSSQL=1)
endif()

if(CPPLINQ_ENABLE_MYSQL)
    if(WIN32)
        target_link_libraries(cpplinq PUBLIC odbc32)
    else()
        find_package(ODBC REQUIRED)
        target_link_libraries(cpplinq PUBLIC ODBC::ODBC)
    endif()
    target_compile_definitions(cpplinq PUBLIC CPPLINQ_HAS_MYSQL=1)
endif()

if(CPPLINQ_ENABLE_INFORMIX)
    if(WIN32)
        target_link_libraries(cpplinq PUBLIC odbc32)
    else()
        find_package(ODBC REQUIRED)
        target_link_libraries(cpplinq PUBLIC ODBC::ODBC)
    endif()
    target_compile_definitions(cpplinq PUBLIC CPPLINQ_HAS_INFORMIX=1)
endif()

if(CPPLINQ_ENABLE_ORACLE)
    if(WIN32)
        target_link_libraries(cpplinq PUBLIC odbc32)
    else()
        find_package(ODBC REQUIRED)
        target_link_libraries(cpplinq PUBLIC ODBC::ODBC)
    endif()
    target_compile_definitions(cpplinq PUBLIC CPPLINQ_HAS_ORACLE=1)
endif()
