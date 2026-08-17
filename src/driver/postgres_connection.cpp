#include "driver/postgres_connection.h"

#ifdef CPPLINQ_HAS_POSTGRES

namespace cpplinq {

PgConnection::PgConnection(std::string connection_string)
    : OdbcConnection(std::move(connection_string))
{}

const ISqlDialect& PgConnection::dialect() const {
    return dialect_;
}

DriverCapabilities PgConnection::capabilities() const {
    DriverCapabilities caps;
    caps.cancel = true;
    caps.streaming = true;
    caps.query_timeout = true;
    caps.transactions = true;
    caps.savepoints = true;
    caps.returning_clause = true;
    caps.output_clause = false;
    caps.upsert = true;
    caps.array_batch_insert = true;
    caps.default_batch_chunk_size = 1000;
    caps.window_functions = true;
    caps.ctes = true;
    return caps;
}

std::string PgConnection::get_driver_display_name() const {
    return "PostgreSQL";
}

DriverInfo PgConnection::get_default_driver_info() const {
    DriverInfo i;
    i.driver_name = "PostgreSQL ODBC Driver";
    i.dbms_name = "PostgreSQL";
    return i;
}

std::vector<std::string> PgConnection::get_connection_candidates(const std::string& conn_str) const {
    std::string trimmed = conn_str;
    while (!trimmed.empty() && (trimmed.front() == ' ' || trimmed.front() == '\t')) trimmed.erase(trimmed.begin());
    while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\t')) trimmed.pop_back();

    std::vector<std::string> candidates;
    if (trimmed.find('=') == std::string::npos && trimmed.rfind("postgres://", 0) != 0 && trimmed.rfind("postgresql://", 0) != 0) {
        // Plain DSN name
        candidates.push_back("DSN=" + trimmed + ";");
        candidates.push_back(trimmed);
    } else if (trimmed.find("Driver=") != std::string::npos || trimmed.find("driver=") != std::string::npos ||
               trimmed.find("DSN=") != std::string::npos || trimmed.find("dsn=") != std::string::npos) {
        candidates.push_back(trimmed);
    } else {
        // Build driver candidate list
        candidates.push_back("Driver={PostgreSQL Unicode};" + trimmed);
        candidates.push_back("Driver={PostgreSQL ANSI};" + trimmed);
        candidates.push_back("Driver={PostgreSQL Unicode(x64)};" + trimmed);
        candidates.push_back("Driver={PostgreSQL ANSI(x64)};" + trimmed);
        candidates.push_back("Driver={PostgreSQL};" + trimmed);
        candidates.push_back(trimmed);
        candidates.push_back("DSN=" + trimmed + ";");
    }
    return candidates;
}

// ----------------------------------------------------------------------------
// make_connection<postgres> Specialization
// ----------------------------------------------------------------------------

template <>
std::unique_ptr<IConnection> make_connection<postgres>(const std::string& connection_string) {
    return std::make_unique<PgConnection>(connection_string);
}

} // namespace cpplinq

#endif // CPPLINQ_HAS_POSTGRES
