#include "driver/mysql_connection.h"

#ifdef CPPLINQ_HAS_MYSQL

namespace cpplinq {

MysqlConnection::MysqlConnection(std::string connection_string)
    : OdbcConnection(std::move(connection_string))
{}

const ISqlDialect& MysqlConnection::dialect() const {
    return dialect_;
}

DriverCapabilities MysqlConnection::capabilities() const {
    DriverCapabilities caps;
    caps.cancel = true;
    caps.streaming = true;
    caps.query_timeout = true;
    caps.transactions = true;
    caps.savepoints = true;
    caps.returning_clause = false;
    caps.output_clause = false;
    caps.upsert = true;
    caps.array_batch_insert = true;
    caps.default_batch_chunk_size = 1000;
    caps.window_functions = true;
    caps.ctes = true;
    return caps;
}

std::string MysqlConnection::get_driver_display_name() const {
    return "MySQL Server";
}

DriverInfo MysqlConnection::get_default_driver_info() const {
    DriverInfo i;
    i.driver_name = "MySQL ODBC Driver";
    i.dbms_name = "MySQL";
    return i;
}

std::vector<std::string> MysqlConnection::get_connection_candidates(const std::string& conn_str) const {
    std::string trimmed = conn_str;
    while (!trimmed.empty() && (trimmed.front() == ' ' || trimmed.front() == '\t')) trimmed.erase(trimmed.begin());
    while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\t')) trimmed.pop_back();

    std::vector<std::string> candidates;
    if (trimmed.find('=') == std::string::npos) {
        // Plain DSN name
        candidates.push_back("DSN=" + trimmed + ";");
        candidates.push_back(trimmed);
    } else {
        candidates.push_back(trimmed);
    }
    return candidates;
}

// ----------------------------------------------------------------------------
// make_connection<mysql> Specialization
// ----------------------------------------------------------------------------

template <>
std::unique_ptr<IConnection> make_connection<mysql>(const std::string& connection_string) {
    return std::make_unique<MysqlConnection>(connection_string);
}

} // namespace cpplinq

#endif // CPPLINQ_HAS_MYSQL
