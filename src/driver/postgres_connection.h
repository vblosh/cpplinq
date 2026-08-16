#pragma once
#include "cpplinq/driver/connection.h"
#include "dialect/postgres_dialect.h"

#ifdef CPPLINQ_HAS_POSTGRES
#include <libpq-fe.h>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace cpplinq {

class PgDataReader : public IDataReader {
public:
    explicit PgDataReader(PGresult* res);
    ~PgDataReader() override;

    bool next() override;
    int column_count() const override;

    bool is_null(int col) const override;
    int64_t get_int64(int col) const override;
    double get_double(int col) const override;
    std::string get_string(int col) const override;
    bool get_bool(int col) const override;
    std::vector<uint8_t> get_blob(int col) const override;

private:
    PGresult* res_;
    int current_row_ = -1;
    int total_rows_ = 0;
};

class PgPreparedStatement : public IPreparedStatement {
public:
    PgPreparedStatement(PGconn* conn, std::string_view sql);
    ~PgPreparedStatement() override = default;

    void bind(int index, const BoundValue& value) override;
    std::unique_ptr<IDataReader> execute_query() override;
    size_t execute_non_query() override;
    void reset() override;

private:
    PGconn* conn_;
    std::string sql_;
    std::string stmt_name_;
    std::vector<BoundValue> params_;
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

    void begin_transaction() override;
    void commit() override;
    void rollback() override;

    const ISqlDialect& dialect() const override;

private:
    std::string connection_string_;
    PGconn* conn_ = nullptr;
    PostgresDialect dialect_;
};

} // namespace cpplinq

#endif // CPPLINQ_HAS_POSTGRES
