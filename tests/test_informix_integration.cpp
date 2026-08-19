#include "integration_test_suite.h"

#ifdef CPPLINQ_HAS_INFORMIX
struct InformixParams {
    using backend_type = cpplinq::informix;
    static constexpr const char* name = "Informix";

    static bool is_enabled() {
        const char* env_conn = std::getenv("CPPLINQ_INFORMIX_ODBC");
        if (env_conn != nullptr && env_conn[0] != '\0') return true;
        const char* cppdb_conn = std::getenv("CPPDB_INFORMIX_ODBC");
        return cppdb_conn != nullptr && cppdb_conn[0] != '\0';
    }

    static std::string skip_reason() {
        return "CPPLINQ_INFORMIX_ODBC / CPPDB_INFORMIX_ODBC environment variable is not set.";
    }

    static std::string connection_string() {
        const char* env_conn = std::getenv("CPPLINQ_INFORMIX_ODBC");
        if (env_conn && env_conn[0] != '\0') return env_conn;
        const char* cppdb_conn = std::getenv("CPPDB_INFORMIX_ODBC");
        return cppdb_conn ? cppdb_conn : "";
    }

    static void clean_tables(cpplinq::DbContext<backend_type>& db) {
        auto drop_table_safe = [&](const char* tbl) {
            try {
                db.execute_raw(std::string("DROP TABLE \"") + tbl + "\"");
            } catch (...) {}
        };
        drop_table_safe("test_measurements");
        drop_table_safe("test_employees");
        drop_table_safe("test_events");
        drop_table_safe("test_accounts");
        drop_table_safe("test_orders");
        drop_table_safe("test_users");
    }
};

INSTANTIATE_TYPED_TEST_SUITE_P(Informix, IntegrationTestSuite, InformixParams);
#endif
