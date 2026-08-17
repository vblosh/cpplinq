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


// ----------------------------------------------------------------------------
// make_connection<mssql> Specialization
// ----------------------------------------------------------------------------

template <>
std::unique_ptr<IConnection> make_connection<mssql>(const std::string& connection_string) {
    return std::make_unique<MssqlConnection>(connection_string);
}

} // namespace cpplinq

#endif // CPPLINQ_HAS_MSSQL
