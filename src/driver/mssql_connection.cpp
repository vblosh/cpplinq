#include "driver/mssql_connection.h"

#ifdef CPPLINQ_HAS_MSSQL

namespace cpplinq {

MssqlConnection::MssqlConnection(std::string connection_string)
    : OdbcConnection(std::move(connection_string))
{}

const ISqlDialect& MssqlConnection::dialect() const {
    return dialect_;
}

DriverCapabilities MssqlConnection::capabilities() const {
    DriverCapabilities caps;
    caps.cancel = true;
    caps.streaming = true;
    caps.query_timeout = true;
    caps.transactions = true;
    caps.savepoints = true;
    caps.returning_clause = false;
    caps.output_clause = true;
    caps.upsert = true;
    caps.array_batch_insert = true;
    caps.default_batch_chunk_size = 1000;
    caps.window_functions = true;
    caps.ctes = true;
    return caps;
}

std::string MssqlConnection::get_driver_display_name() const {
    return "Microsoft SQL Server";
}

DriverInfo MssqlConnection::get_default_driver_info() const {
    DriverInfo i;
    i.driver_name = "ODBC Driver for SQL Server";
    i.dbms_name = "Microsoft SQL Server";
    return i;
}

std::vector<std::string> MssqlConnection::get_connection_candidates(const std::string& conn_str) const {
    std::vector<std::string> candidates;
    std::string trimmed = conn_str;
    while (!trimmed.empty() && (trimmed.front() == ' ' || trimmed.front() == '\t')) trimmed.erase(trimmed.begin());
    while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\t')) trimmed.pop_back();

    if (trimmed == "MSSQLLocalDB" || trimmed == "(localdb)\\MSSQLLocalDB" || trimmed == "localdb") {
        candidates.push_back("Driver={ODBC Driver 18 for SQL Server};Server=(localdb)\\MSSQLLocalDB;Database=tempdb;Trusted_Connection=yes;TrustServerCertificate=yes;");
        candidates.push_back("Driver={ODBC Driver 17 for SQL Server};Server=(localdb)\\MSSQLLocalDB;Database=tempdb;Trusted_Connection=yes;");
        candidates.push_back("Driver={SQL Server};Server=(localdb)\\MSSQLLocalDB;Database=tempdb;Trusted_Connection=yes;");
        candidates.push_back("DSN=MSSQLLocalDB;");
    } else if (trimmed.find("Driver=") == std::string::npos && trimmed.find("DRIVER=") == std::string::npos &&
               trimmed.rfind("DSN=", 0) != 0 && trimmed.rfind("dsn=", 0) != 0) {
        if (trimmed.find('=') == std::string::npos) {
            candidates.push_back("DSN=" + trimmed);
            candidates.push_back("Driver={ODBC Driver 18 for SQL Server};Server=" + trimmed + ";Database=tempdb;Trusted_Connection=yes;TrustServerCertificate=yes;");
            candidates.push_back("Driver={SQL Server};Server=" + trimmed + ";Database=tempdb;Trusted_Connection=yes;");
        } else {
            candidates.push_back("Driver={ODBC Driver 18 for SQL Server};TrustServerCertificate=yes;" + trimmed);
            candidates.push_back("Driver={ODBC Driver 17 for SQL Server};" + trimmed);
            candidates.push_back("Driver={SQL Server};" + trimmed);
            candidates.push_back(trimmed);
        }
    } else {
        candidates.push_back(trimmed);
    }
    return candidates;
}

// ----------------------------------------------------------------------------
// make_connection<mssql> Specialization
// ----------------------------------------------------------------------------

template <>
std::unique_ptr<IConnection> make_connection<mssql>(const std::string& connection_string) {
    return std::make_unique<MssqlConnection>(connection_string);
}

} // namespace cpplinq

#endif // CPPLINQ_HAS_MSSQL
