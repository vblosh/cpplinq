# Google Benchmark via FetchContent
include(FetchContent)
FetchContent_Declare(
    googlebenchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG v1.9.1
    GIT_SHALLOW TRUE
)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googlebenchmark)

# --- PostgreSQL Benchmark executable ---
if(CPPLINQ_ENABLE_POSTGRES)
    find_package(PostgreSQL QUIET)
    if(PostgreSQL_FOUND)
        message(STATUS "libpq found: ${PostgreSQL_LIBRARIES}")
        set(HAS_LIBPQ TRUE)
    else()
        message(STATUS "libpq NOT found — native PostgreSQL benchmarks will be disabled")
        set(HAS_LIBPQ FALSE)
    endif()

    add_executable(bench_postgres_perf bench_postgres_perf.cpp)
    target_link_libraries(bench_postgres_perf PRIVATE
        cpplinq
        benchmark::benchmark
        benchmark::benchmark_main
    )
    target_compile_features(bench_postgres_perf PRIVATE cxx_std_20)

    if(HAS_LIBPQ)
        target_include_directories(bench_postgres_perf PRIVATE ${PostgreSQL_INCLUDE_DIRS})
        target_link_libraries(bench_postgres_perf PRIVATE ${PostgreSQL_LIBRARIES})
        target_compile_definitions(bench_postgres_perf PRIVATE HAS_LIBPQ=1)
    endif()

    # Register with CTest for easy discovery
    add_test(NAME bench_postgres_perf COMMAND bench_postgres_perf --benchmark_min_time=0.5s)
endif()

# --- Informix Benchmark executable ---
if(CPPLINQ_ENABLE_INFORMIX)
    add_executable(bench_informix_perf bench_informix_perf.cpp)
    target_link_libraries(bench_informix_perf PRIVATE
        cpplinq
        benchmark::benchmark
        benchmark::benchmark_main
    )
    target_compile_features(bench_informix_perf PRIVATE cxx_std_20)
    add_test(NAME bench_informix_perf COMMAND bench_informix_perf --benchmark_min_time=0.5s)
endif()

