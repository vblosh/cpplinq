#include "integration_test_suite.h"

#ifdef CPPLINQ_HAS_ORACLE
struct OracleParams {
    using backend_type = cpplinq::oracle;
    static constexpr const char* name = "Oracle";

    static bool is_enabled() {
        const char* env_conn = std::getenv("CPPLINQ_ORACLE_ODBC");
        if (env_conn != nullptr && env_conn[0] != '\0') return true;
        const char* cppdb_conn = std::getenv("CPPDB_ORACLE_ODBC");
        return cppdb_conn != nullptr && cppdb_conn[0] != '\0';
    }

    static std::string skip_reason() {
        return "CPPLINQ_ORACLE_ODBC / CPPDB_ORACLE_ODBC environment variable is not set.";
    }

    static std::string connection_string() {
        const char* env_conn = std::getenv("CPPLINQ_ORACLE_ODBC");
        if (env_conn && env_conn[0] != '\0') return env_conn;
        const char* cppdb_conn = std::getenv("CPPDB_ORACLE_ODBC");
        return cppdb_conn ? cppdb_conn : "";
    }

    static void clean_tables(cpplinq::DbContext<backend_type>& db) {
        auto drop_table_safe = [&](const char* tbl) {
            try {
                db.execute_raw(std::string("DROP TABLE \"") + tbl + "\" CASCADE CONSTRAINTS");
            } catch (...) {
                try {
                    db.execute_raw(std::string("DROP TABLE \"") + tbl + "\"");
                } catch (...) {}
            }
        };
        drop_table_safe("test_measurements");
        drop_table_safe("test_employees");
        drop_table_safe("test_events");
        drop_table_safe("test_accounts");
        drop_table_safe("test_orders");
        drop_table_safe("test_users");
    }
};

INSTANTIATE_TYPED_TEST_SUITE_P(Oracle, IntegrationTestSuite, OracleParams);
#endif
