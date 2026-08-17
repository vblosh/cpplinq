#pragma once
#include "driver/odbc_connection.h"
#include "dialect/mysql_dialect.h"

#ifdef CPPLINQ_HAS_MYSQL

namespace cpplinq {

using MysqlDataReader = OdbcDataReader;
using MysqlPreparedStatement = OdbcPreparedStatement;

class MysqlConnection : public OdbcConnection {
public:
    explicit MysqlConnection(std::string connection_string);

    const ISqlDialect& dialect() const override;
    DriverCapabilities capabilities() const override;

protected:
    std::vector<std::string> get_connection_candidates(const std::string& conn_str) const override;
    DriverInfo get_default_driver_info() const override;
    std::string get_driver_display_name() const override;

private:
    MysqlDialect dialect_;
};

template <>
std::unique_ptr<IConnection> make_connection<mysql>(const std::string& connection_string);

} // namespace cpplinq

#endif // CPPLINQ_HAS_MYSQL
