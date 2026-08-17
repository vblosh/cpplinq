#pragma once
#include "driver/odbc_connection.h"
#include "dialect/postgres_dialect.h"

#ifdef CPPLINQ_HAS_POSTGRES

namespace cpplinq {

using PgDataReader = OdbcDataReader;
using PgPreparedStatement = OdbcPreparedStatement;

class PgConnection : public OdbcConnection {
public:
    explicit PgConnection(std::string connection_string);

    const ISqlDialect& dialect() const override;
    DriverCapabilities capabilities() const override;

protected:
    std::vector<std::string> get_connection_candidates(const std::string& conn_str) const override;
    DriverInfo get_default_driver_info() const override;
    std::string get_driver_display_name() const override;

private:
    PostgresDialect dialect_;
};

template <>
std::unique_ptr<IConnection> make_connection<postgres>(const std::string& connection_string);

} // namespace cpplinq

#endif // CPPLINQ_HAS_POSTGRES
