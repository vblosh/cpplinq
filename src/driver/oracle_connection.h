#pragma once
#include "driver/odbc_connection.h"
#include "dialect/oracle_dialect.h"

#ifdef CPPLINQ_HAS_ORACLE

namespace cpplinq {

using OracleDataReader = OdbcDataReader;
using OraclePreparedStatement = OdbcPreparedStatement;

class OracleConnection : public OdbcConnection {
public:
    explicit OracleConnection(std::string connection_string);

    const ISqlDialect& dialect() const override;
    DriverCapabilities capabilities() const override;

protected:
    DriverInfo get_default_driver_info() const override;
    std::string get_driver_display_name() const override;

private:
    OracleDialect dialect_;
};

template <>
std::unique_ptr<IConnection> make_connection<oracle>(const std::string& connection_string);

} // namespace cpplinq

#endif // CPPLINQ_HAS_ORACLE
