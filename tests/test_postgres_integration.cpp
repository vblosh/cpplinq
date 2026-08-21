#include "integration_test_suite.h"

#ifdef CPPLINQ_HAS_POSTGRES
struct PostgresParams {
    using backend_type = cpplinq::postgres;
    static constexpr const char* name = "PostgreSQL";

    static bool is_enabled() {
        const char* env_libpq = std::getenv("CPPLINQ_POSTGRES_LIBPQ");
        if (env_libpq != nullptr && env_libpq[0] != '\0') return true;
        const char* env_odbc = std::getenv("CPPLINQ_POSTGRES_ODBC");
        return env_odbc != nullptr && env_odbc[0] != '\0';
    }

    static std::string skip_reason() {
        return "CPPLINQ_POSTGRES_LIBPQ or CPPLINQ_POSTGRES_ODBC environment variable is not set.";
    }

    static std::string connection_string() {
        const char* env_libpq = std::getenv("CPPLINQ_POSTGRES_LIBPQ");
        if (env_libpq && env_libpq[0] != '\0') return env_libpq;
        const char* env_odbc = std::getenv("CPPLINQ_POSTGRES_ODBC");
        return env_odbc ? env_odbc : "";
    }

    static void clean_tables(cpplinq::DbContext<backend_type>& db) {
        try {
            db.execute_raw("DROP TABLE IF EXISTS \"test_measurements\" CASCADE");
            db.execute_raw("DROP TABLE IF EXISTS \"test_employees\" CASCADE");
            db.execute_raw("DROP TABLE IF EXISTS \"test_events\" CASCADE");
            db.execute_raw("DROP TABLE IF EXISTS \"test_accounts\" CASCADE");
            db.execute_raw("DROP TABLE IF EXISTS \"test_orders\" CASCADE");
            db.execute_raw("DROP TABLE IF EXISTS \"test_users\" CASCADE");
        } catch (...) {}
    }
};

INSTANTIATE_TYPED_TEST_SUITE_P(Postgres, IntegrationTestSuite, PostgresParams);
#endif
