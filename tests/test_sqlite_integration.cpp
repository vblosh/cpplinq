#include "integration_test_suite.h"

struct SqliteParams {
    using backend_type = cpplinq::sqlite;
    static constexpr const char* name = "SQLite";

    static bool is_enabled() {
        return true;
    }

    static std::string skip_reason() {
        return "";
    }

    static std::string connection_string() {
        return ":memory:";
    }

    static void clean_tables(cpplinq::DbContext<backend_type>& /*db*/) {}
};

INSTANTIATE_TYPED_TEST_SUITE_P(Sqlite, IntegrationTestSuite, SqliteParams);
