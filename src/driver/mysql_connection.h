#pragma once
#include "cpplinq/driver/connection.h"
#include "dialect/mysql_dialect.h"
#if __has_include(<mariadb/mysql.h>)
#include <mariadb/mysql.h>
#elif __has_include(<mysql/mysql.h>)
#include <mysql/mysql.h>
#else
#include <mysql.h>
#endif
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <stop_token>
#include <functional>

#ifdef CPPLINQ_HAS_MYSQL

namespace cpplinq {

class MysqlDataReader : public IDataReader {
public:
    explicit MysqlDataReader(std::shared_ptr<MYSQL_RES> res);
    ~MysqlDataReader() override = default;

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
    std::shared_ptr<MYSQL_RES> res_;
    int num_fields_ = 0;
    MYSQL_FIELD* fields_ = nullptr;
    MYSQL_ROW current_row_ = nullptr;
    unsigned long* current_lengths_ = nullptr;
};

class MysqlStmtDataReader : public IDataReader {
public:
    explicit MysqlStmtDataReader(MYSQL_STMT* stmt, std::shared_ptr<MYSQL_RES> meta);
    ~MysqlStmtDataReader() override;

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
    struct ColumnBind {
        std::vector<char> buffer;
        unsigned long length = 0;
        my_bool is_null = 0;
        my_bool error = 0;
    };

    MYSQL_STMT* stmt_ = nullptr;
    std::shared_ptr<MYSQL_RES> meta_;
    int num_fields_ = 0;
    MYSQL_FIELD* fields_ = nullptr;
    std::vector<ColumnBind> col_binds_;
    std::vector<MYSQL_BIND> binds_;
    bool has_fetched_ = false;
};

class MysqlPreparedStatement : public IPreparedStatement {
public:
    explicit MysqlPreparedStatement(MYSQL* conn, std::string sql);
    ~MysqlPreparedStatement() override;

    void bind(int index, const BoundValue& value) override;
    std::unique_ptr<IDataReader> execute_query() override;
    size_t execute_non_query() override;
    void reset() override;

    void cancel() override;
    void set_timeout(uint32_t seconds) override;
    void set_stop_token(std::stop_token token) override;

private:
    struct ParamStorage {
        enum_field_types type = MYSQL_TYPE_NULL;
        std::vector<char> buffer;
        unsigned long length = 0;
        my_bool is_null = 1;
        bool is_unsigned = false;
    };

    MYSQL* conn_ = nullptr;
    MYSQL_STMT* stmt_ = nullptr;
    std::string sql_;
    unsigned long param_count_ = 0;
    std::vector<ParamStorage> params_;
    std::vector<MYSQL_BIND> binds_;
    std::optional<std::stop_token> stop_token_;
    std::optional<std::stop_callback<std::function<void()>>> stop_cb_;

    void sync_binds();
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
    MYSQL* conn_ = nullptr;
    MysqlDialect dialect_;

    struct ParsedParams {
        std::string host = "127.0.0.1";
        unsigned int port = 3306;
        std::string user = "cppdb";
        std::string password = "cppdb_password";
        std::string db = "cppdb";
        std::string unix_socket;
        unsigned int timeout_sec = 10;
        std::string charset = "utf8mb4";
    };

    ParsedParams parse_connection_params() const;
};

template <>
std::unique_ptr<IConnection> make_connection<mysql>(const std::string& connection_string);

} // namespace cpplinq

#endif // CPPLINQ_HAS_MYSQL
