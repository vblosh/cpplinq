#include "driver/oracle_connection.h"

#ifdef CPPLINQ_HAS_ORACLE

namespace cpplinq {

OracleConnection::OracleConnection(std::string connection_string)
    : OdbcConnection(std::move(connection_string))
{}

const ISqlDialect& OracleConnection::dialect() const {
    return dialect_;
}

DriverCapabilities OracleConnection::capabilities() const {
    DriverCapabilities caps;
    caps.cancel = true;
    caps.streaming = true;
    caps.query_timeout = true;
    caps.transactions = true;
    caps.savepoints = true;
    caps.returning_clause = false;
    caps.output_clause = false;
    caps.upsert = true;
    caps.array_batch_insert = false;
    caps.default_batch_chunk_size = 1000;
    caps.window_functions = true;
    caps.ctes = true;
    return caps;
}

std::string OracleConnection::get_driver_display_name() const {
    return "Oracle";
}

DriverInfo OracleConnection::get_default_driver_info() const {
    DriverInfo i;
    i.driver_name = "Oracle ODBC Driver";
    i.dbms_name = "Oracle";
    return i;
}


// ----------------------------------------------------------------------------
// make_connection<oracle> Specialization
// ----------------------------------------------------------------------------

template <>
std::unique_ptr<IConnection> make_connection<oracle>(const std::string& connection_string) {
    return std::make_unique<OracleConnection>(connection_string);
}

} // namespace cpplinq

#endif // CPPLINQ_HAS_ORACLE
