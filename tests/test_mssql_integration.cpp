#include "integration_test_suite.h"

#ifdef CPPLINQ_HAS_MSSQL
struct MssqlParams {
    using backend_type = cpplinq::mssql;
    static constexpr const char* name = "MSSQL";

    static bool is_enabled() {
        const char* env_conn = std::getenv("CPPLINQ_MSSQL_ODBC");
        return env_conn != nullptr && env_conn[0] != '\0';
    }

    static std::string skip_reason() {
        return "CPPLINQ_MSSQL_ODBC environment variable is not set.";
    }

    static std::string connection_string() {
        const char* env_conn = std::getenv("CPPLINQ_MSSQL_ODBC");
        return env_conn ? env_conn : "";
    }

    static void clean_tables(cpplinq::DbContext<backend_type>& db) {
        try {
            db.execute_raw("IF OBJECT_ID(N'test_measurements', N'U') IS NOT NULL DROP TABLE [test_measurements];");
            db.execute_raw("IF OBJECT_ID(N'test_employees', N'U') IS NOT NULL DROP TABLE [test_employees];");
            db.execute_raw("IF OBJECT_ID(N'test_events', N'U') IS NOT NULL DROP TABLE [test_events];");
            db.execute_raw("IF OBJECT_ID(N'test_accounts', N'U') IS NOT NULL DROP TABLE [test_accounts];");
            db.execute_raw("IF OBJECT_ID(N'test_orders', N'U') IS NOT NULL DROP TABLE [test_orders];");
            db.execute_raw("IF OBJECT_ID(N'test_users', N'U') IS NOT NULL DROP TABLE [test_users];");
        } catch (...) {}
    }
};

INSTANTIATE_TYPED_TEST_SUITE_P(Mssql, IntegrationTestSuite, MssqlParams);
#endif
