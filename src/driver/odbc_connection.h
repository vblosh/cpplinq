#pragma once

#if defined(CPPLINQ_HAS_MSSQL) || defined(CPPLINQ_HAS_MYSQL) || defined(CPPLINQ_HAS_POSTGRES) || defined(CPPLINQ_HAS_INFORMIX)

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include <sql.h>
#include <sqlext.h>

#include "cpplinq/driver/connection.h"
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <stop_token>
#include <functional>
#include <optional>

namespace cpplinq {

class OdbcDataReader : public IDataReader {
public:
    explicit OdbcDataReader(SQLHSTMT hstmt, bool owns_stmt = false);
    ~OdbcDataReader() override;

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
    struct BoundCol {
        SQLSMALLINT data_type = 0;
        SQLSMALLINT c_type = 0;
        SQLULEN col_size = 0;
        SQLSMALLINT dec_digits = 0;
        SQLSMALLINT nullable = 0;
        SQLLEN ind = SQL_NULL_DATA;

        union {
            int64_t int_val = 0;
            uint64_t uint_val;
            double double_val;
            unsigned char bool_val;
            DATE_STRUCT date_val;
            TIME_STRUCT time_val;
            TIMESTAMP_STRUCT timestamp_val;
            SQL_INTERVAL_STRUCT interval_val;
            SQLGUID guid_val;
        };

        std::vector<uint8_t> buffer;
        mutable std::string str_cache;
        mutable std::wstring wstr_cache;

        BoundCol() : int_val(0) {}
    };

    void init_bound_columns();
    void ensure_str(const BoundCol& c) const;

    SQLHSTMT hstmt_ = SQL_NULL_HSTMT;
    bool owns_stmt_ = false;
    SQLSMALLINT col_count_ = 0;
    std::vector<BoundCol> bound_cols_;
};

class OdbcPreparedStatement : public IPreparedStatement {
public:
    OdbcPreparedStatement(SQLHDBC hdbc, std::string_view sql);
    ~OdbcPreparedStatement() override;

    void bind(int index, const BoundValue& value) override;
    std::unique_ptr<IDataReader> execute_query() override;
    size_t execute_non_query() override;
    void reset() override;

    void cancel() override;
    void set_timeout(uint32_t seconds) override;
    void set_stop_token(std::stop_token token) override;

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
    std::optional<std::stop_token> stop_token_;
    std::optional<std::stop_callback<std::function<void()>>> stop_cb_;
};

class OdbcConnection : public IConnection {
public:
    explicit OdbcConnection(std::string connection_string);
    ~OdbcConnection() override;

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

    DriverInfo info() const override;

    size_t insert_many_batch(
        std::string_view sql,
        const std::vector<BoundValue>& flat_params,
        size_t col_count,
        size_t row_count
    ) override;

protected:
    virtual DriverInfo get_default_driver_info() const = 0;
    virtual std::string get_driver_display_name() const = 0;

    std::string connection_string_;
    SQLHENV henv_ = SQL_NULL_HENV;
    SQLHDBC hdbc_ = SQL_NULL_HDBC;
    bool is_open_ = false;
};

} // namespace cpplinq

#endif // defined(CPPLINQ_HAS_MSSQL) || defined(CPPLINQ_HAS_MYSQL) || defined(CPPLINQ_HAS_POSTGRES) || defined(CPPLINQ_HAS_INFORMIX)
