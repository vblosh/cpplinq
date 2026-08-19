#include "integration_test_suite.h"

#ifdef CPPLINQ_HAS_MYSQL
struct MysqlParams {
    using backend_type = cpplinq::mysql;
    static constexpr const char* name = "MySQL";

    static bool is_enabled() {
        const char* env_conn = std::getenv("CPPLINQ_MYSQL_ODBC");
        return env_conn != nullptr && env_conn[0] != '\0';
    }

    static std::string skip_reason() {
        return "CPPLINQ_MYSQL_ODBC environment variable is not set.";
    }

    static std::string connection_string() {
        const char* env_conn = std::getenv("CPPLINQ_MYSQL_ODBC");
        return env_conn ? env_conn : "";
    }

    static void clean_tables(cpplinq::DbContext<backend_type>& db) {
        try {
            db.execute_raw("DROP TABLE IF EXISTS `test_measurements`");
            db.execute_raw("DROP TABLE IF EXISTS `test_employees`");
            db.execute_raw("DROP TABLE IF EXISTS `test_events`");
            db.execute_raw("DROP TABLE IF EXISTS `test_accounts`");
            db.execute_raw("DROP TABLE IF EXISTS `test_orders`");
            db.execute_raw("DROP TABLE IF EXISTS `test_users`");
        } catch (...) {}
    }
};

INSTANTIATE_TYPED_TEST_SUITE_P(Mysql, IntegrationTestSuite, MysqlParams);
#endif
