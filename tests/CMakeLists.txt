if(NOT TARGET GTest::gtest)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

set(TEST_TARGETS test_expression test_query_builder test_row_mapper test_sqlite_integration test_connection_pool test_streaming)
if(CPPLINQ_ENABLE_POSTGRES)
    list(APPEND TEST_TARGETS test_postgres_integration)
endif()
if(CPPLINQ_ENABLE_MSSQL)
    list(APPEND TEST_TARGETS test_mssql_integration)
endif()
if(CPPLINQ_ENABLE_MYSQL)
    list(APPEND TEST_TARGETS test_mysql_integration)
endif()

foreach(target ${TEST_TARGETS})
    add_executable(${target} ${target}.cpp)
    target_include_directories(${target} BEFORE PRIVATE ${googletest_SOURCE_DIR}/googletest/include)
    target_compile_definitions(${target} PRIVATE GTEST_LINKED_AS_SHARED_LIBRARY=0)
    target_link_libraries(${target} PRIVATE cpplinq GTest::gtest GTest::gtest_main)
    add_test(NAME ${target} COMMAND ${target})
endforeach()
