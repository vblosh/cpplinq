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


// ----------------------------------------------------------------------------
// make_connection<postgres> Specialization
// ----------------------------------------------------------------------------

template <>
std::unique_ptr<IConnection> make_connection<postgres>(const std::string& connection_string) {
    return std::make_unique<PgConnection>(connection_string);
}

} // namespace cpplinq

#endif // CPPLINQ_HAS_POSTGRES
