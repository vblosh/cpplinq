#pragma once
#include "driver/odbc_connection.h"
#include "dialect/mssql_dialect.h"

#ifdef CPPLINQ_HAS_MSSQL

namespace cpplinq {

using MssqlDataReader = OdbcDataReader;
using MssqlPreparedStatement = OdbcPreparedStatement;

class MssqlConnection : public OdbcConnection {
public:
    explicit MssqlConnection(std::string connection_string);

    const ISqlDialect& dialect() const override;
    DriverCapabilities capabilities() const override;

protected:
    DriverInfo get_default_driver_info() const override;
    std::string get_driver_display_name() const override;

private:
    MssqlDialect dialect_;
};

template <>
std::unique_ptr<IConnection> make_connection<mssql>(const std::string& connection_string);

} // namespace cpplinq

#endif // CPPLINQ_HAS_MSSQL
