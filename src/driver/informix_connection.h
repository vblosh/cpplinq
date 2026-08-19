#pragma once
#include "driver/odbc_connection.h"
#include "dialect/informix_dialect.h"

#ifdef CPPLINQ_HAS_INFORMIX

namespace cpplinq {

using InformixDataReader = OdbcDataReader;
using InformixPreparedStatement = OdbcPreparedStatement;

class InformixConnection : public OdbcConnection {
public:
    explicit InformixConnection(std::string connection_string);

    const ISqlDialect& dialect() const override;
    DriverCapabilities capabilities() const override;

protected:
    DriverInfo get_default_driver_info() const override;
    std::string get_driver_display_name() const override;

private:
    InformixDialect dialect_;
};

template <>
std::unique_ptr<IConnection> make_connection<informix>(const std::string& connection_string);

} // namespace cpplinq

#endif // CPPLINQ_HAS_INFORMIX
