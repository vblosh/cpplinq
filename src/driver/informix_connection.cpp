#include "driver/informix_connection.h"

#ifdef CPPLINQ_HAS_INFORMIX

#include <algorithm>

namespace cpplinq {

namespace {
std::string ensure_informix_conn_params(std::string conn_str) {
    std::string upper = conn_str;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
    if (conn_str.find('=') == std::string::npos && !conn_str.empty()) {
        conn_str = "DSN=" + conn_str + ";";
        upper = "DSN=" + upper + ";";
    }
    if (upper.find("DELIMIDENT") == std::string::npos) {
        conn_str += "DELIMIDENT=Y;";
    }
    if (upper.find("CLIENT_LOCALE") == std::string::npos) {
        conn_str += "CLIENT_LOCALE=en_us.utf8;";
    }
    if (upper.find("REPORT_CONVERT_ERROR") == std::string::npos) {
        conn_str += "REPORT_CONVERT_ERROR=0;";
    }
    return conn_str;
}
} // namespace

InformixConnection::InformixConnection(std::string connection_string)
    : OdbcConnection(ensure_informix_conn_params(std::move(connection_string)))
{}

const ISqlDialect& InformixConnection::dialect() const {
    return dialect_;
}

DriverCapabilities InformixConnection::capabilities() const {
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

std::string InformixConnection::get_driver_display_name() const {
    return "IBM Informix";
}

DriverInfo InformixConnection::get_default_driver_info() const {
    DriverInfo i;
    i.driver_name = "IBM INFORMIX ODBC DRIVER (64-bit)";
    i.dbms_name = "Informix";
    return i;
}


// ----------------------------------------------------------------------------
// make_connection<informix> Specialization
// ----------------------------------------------------------------------------

template <>
std::unique_ptr<IConnection> make_connection<informix>(const std::string& connection_string) {
    return std::make_unique<InformixConnection>(connection_string);
}

} // namespace cpplinq

#endif // CPPLINQ_HAS_INFORMIX
