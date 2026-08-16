#pragma once
#include "cpplinq/driver/connection.h"
#include "dialect/sqlite_dialect.h"
#include <sqlite3.h>
#include <memory>
#include <string>
#include <string_view>

namespace cpplinq {

class SqliteDataReader : public IDataReader {
public:
    explicit SqliteDataReader(std::shared_ptr<sqlite3_stmt> stmt, sqlite3* db = nullptr);
    ~SqliteDataReader() override;

    bool next() override;
    int column_count() const override;

    bool is_null(int col) const override;
    int64_t get_int64(int col) const override;
    double get_double(int col) const override;
    std::string get_string(int col) const override;
    bool get_bool(int col) const override;
    std::vector<uint8_t> get_blob(int col) const override;

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

private:
    sqlite3* db_;
    std::shared_ptr<sqlite3_stmt> stmt_;
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

private:
    std::string connection_string_;
    sqlite3* db_ = nullptr;
    SqliteDialect dialect_;
};

} // namespace cpplinq
