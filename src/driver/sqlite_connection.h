#pragma once
#include "cpplinq/driver/connection.h"
#include "dialect/sqlite_dialect.h"
#include <sqlite3.h>
#include <memory>
#include <string>
#include <string_view>
#include <stop_token>
#include <functional>
#include <optional>

namespace cpplinq {

class SqliteDataReader : public IDataReader {
public:
    explicit SqliteDataReader(std::shared_ptr<sqlite3_stmt> stmt, sqlite3* db = nullptr);
    ~SqliteDataReader() override;

    bool next() override;
    int column_count() const override;

    bool is_null(int col) const override;
    int64_t get_int64(int col) const override;
    uint64_t get_uint64(int col) const override;
    double get_double(int col) const override;
    std::string get_string(int col) const override;
    std::wstring get_wstring(int col) const override;
    bool get_bool(int col) const override;
    std::vector<uint8_t> get_blob(int col) const override;
    SqlNumeric get_numeric(int col) const override;
    SqlDate get_date(int col) const override;
    SqlTime get_time(int col) const override;
    SqlTimestamp get_timestamp(int col) const override;
    SqlInterval get_interval(int col) const override;
    SqlGuid get_guid(int col) const override;

private:
    std::shared_ptr<sqlite3_stmt> stmt_;
    sqlite3* db_;
};

class SqlitePreparedStatement : public IPreparedStatement {
public:
    explicit SqlitePreparedStatement(sqlite3* db, sqlite3_stmt* stmt);
    ~SqlitePreparedStatement() override = default;

    void bind(int index, const BoundValue& value) override;
    std::unique_ptr<IDataReader> execute_query() override;
    size_t execute_non_query() override;
    void reset() override;

    void cancel() override;
    void set_timeout(uint32_t seconds) override;
    void set_stop_token(std::stop_token token) override;

private:
    sqlite3* db_;
    std::shared_ptr<sqlite3_stmt> stmt_;
    std::optional<std::stop_token> stop_token_;
    std::optional<std::stop_callback<std::function<void()>>> stop_cb_;
};

class SqliteConnection : public IConnection {
public:
    explicit SqliteConnection(std::string connection_string);
    ~SqliteConnection() override;

    void open() override;
    void close() override;
    bool is_open() const override;

    std::unique_ptr<IPreparedStatement> prepare(std::string_view sql) override;
    void execute(std::string_view sql) override;

    void begin_transaction() override;
    void commit() override;
    void rollback() override;

    const ISqlDialect& dialect() const override;

    DriverInfo info() const override;
    DriverCapabilities capabilities() const override;

private:
    std::string connection_string_;
    sqlite3* db_ = nullptr;
    SqliteDialect dialect_;
};

} // namespace cpplinq
