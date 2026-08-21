#pragma once
#include "cpplinq/driver/connection.h"
#include "dialect/postgres_dialect.h"
#include <libpq-fe.h>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <stop_token>
#include <functional>

#ifdef CPPLINQ_HAS_POSTGRES

namespace cpplinq {

class PgDataReader : public IDataReader {
public:
    explicit PgDataReader(std::shared_ptr<PGresult> res);
    ~PgDataReader() override = default;

    bool next() override;
    int column_count() const override;

    bool is_null(int col) const override;
    int64_t get_int64(int col) const override;
    uint64_t get_uint64(int col) const override;
    double get_double(int col) const override;
    std::string get_string(int col) const override;
    std::string_view get_string_view(int col) const override;
    std::wstring get_wstring(int col) const override;
    bool get_bool(int col) const override;
    std::vector<uint8_t> get_blob(int col) const override;
    SqlNumeric get_numeric(int col) const override;
    SqlDate get_date(int col) const override;
    SqlTime get_time(int col) const override;
    SqlTimestamp get_timestamp(int col) const override;
    SqlInterval get_interval(int col) const override;
    SqlGuid get_guid(int col) const override;
    BoundValue get_value(int col) const override;

private:
    std::shared_ptr<PGresult> res_;
    int num_rows_ = 0;
    int current_row_ = -1;
    int num_fields_ = 0;
};

class PgPreparedStatement : public IPreparedStatement {
public:
    explicit PgPreparedStatement(PGconn* conn, std::string sql, std::string stmt_name = "");
    ~PgPreparedStatement() override;

    void bind(int index, const BoundValue& value) override;
    std::unique_ptr<IDataReader> execute_query() override;
    size_t execute_non_query() override;
    void reset() override;

    void cancel() override;
    void set_timeout(uint32_t seconds) override;
    void set_stop_token(std::stop_token token) override;

private:
    PGconn* conn_ = nullptr;
    std::string sql_;
    std::string stmt_name_;
    bool is_prepared_ = false;
    std::vector<std::optional<std::string>> param_strings_;
    std::optional<std::stop_token> stop_token_;
    std::optional<std::stop_callback<std::function<void()>>> stop_cb_;

    void ensure_prepared();
};

class PgConnection : public IConnection {
public:
    explicit PgConnection(std::string connection_string);
    ~PgConnection() override;

    void open() override;
    void close() override;
    bool is_open() const override;

    std::unique_ptr<IPreparedStatement> prepare(std::string_view sql) override;
    void execute(std::string_view sql) override;
    std::unique_ptr<IDataReader> execute_query_direct(std::string_view sql) override;
    size_t execute_non_query_direct(std::string_view sql) override;

    void begin_transaction() override;
    void commit() override;
    void rollback() override;

    const ISqlDialect& dialect() const override;

    DriverInfo info() const override;
    DriverCapabilities capabilities() const override;

    size_t insert_many_batch(
        std::string_view sql,
        const std::vector<BoundValue>& flat_params,
        size_t col_count,
        size_t row_count
    ) override;

private:
    std::string connection_string_;
    PGconn* conn_ = nullptr;
    PostgresDialect dialect_;

    std::string resolve_connection_string() const;
};

template <>
std::unique_ptr<IConnection> make_connection<postgres>(const std::string& connection_string);

} // namespace cpplinq

#endif // CPPLINQ_HAS_POSTGRES
