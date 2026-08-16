#pragma once
#include "cpplinq/driver/connection.h"
#include "dialect/mysql_dialect.h"

#ifdef CPPLINQ_HAS_MYSQL

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include <sql.h>
#include <sqlext.h>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace cpplinq {

class MysqlDataReader : public IDataReader {
public:
    explicit MysqlDataReader(SQLHSTMT hstmt, bool owns_stmt = false);
    ~MysqlDataReader() override;

    bool next() override;
    int column_count() const override;

    bool is_null(int col) const override;
    int64_t get_int64(int col) const override;
    double get_double(int col) const override;
    std::string get_string(int col) const override;
    bool get_bool(int col) const override;
    std::vector<uint8_t> get_blob(int col) const override;

private:
    struct CachedCol {
        bool is_null = true;
        int64_t int_val = 0;
        double double_val = 0.0;
        std::string str_val;
        bool bool_val = false;
        std::vector<uint8_t> blob_val;
    };

    void fetch_row_cache();

    SQLHSTMT hstmt_ = SQL_NULL_HSTMT;
    bool owns_stmt_ = false;
    SQLSMALLINT col_count_ = 0;
    std::vector<CachedCol> row_cache_;
};

class MysqlPreparedStatement : public IPreparedStatement {
public:
    MysqlPreparedStatement(SQLHDBC hdbc, std::string_view sql);
    ~MysqlPreparedStatement() override;

    void bind(int index, const BoundValue& value) override;
    std::unique_ptr<IDataReader> execute_query() override;
    size_t execute_non_query() override;
    void reset() override;

private:
    struct ParamStorage {
        std::vector<uint8_t> buffer;
        SQLLEN ind = 0;
        SQLSMALLINT c_type = 0;
        SQLSMALLINT sql_type = 0;
        SQLULEN col_size = 0;
        SQLSMALLINT dec_digits = 0;
    };

    void apply_bindings();

    SQLHDBC hdbc_ = SQL_NULL_HDBC;
    SQLHSTMT hstmt_ = SQL_NULL_HSTMT;
    std::string sql_;
    std::vector<BoundValue> params_;
    std::vector<ParamStorage> storage_;
};

class MysqlConnection : public IConnection {
public:
    explicit MysqlConnection(std::string connection_string);
    ~MysqlConnection() override;

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
    std::string conn_str_;
    bool is_open_ = false;
    bool in_transaction_ = false;
    SQLHENV henv_ = SQL_NULL_HENV;
    SQLHDBC hdbc_ = SQL_NULL_HDBC;
    MysqlDialect dialect_;
};

template <>
std::unique_ptr<IConnection> make_connection<mysql>(const std::string& connection_string);

} // namespace cpplinq

#endif // CPPLINQ_HAS_MYSQL
