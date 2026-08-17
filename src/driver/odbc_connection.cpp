#include "driver/odbc_connection.h"
#include "driver/odbc_utils.h"

#if defined(CPPLINQ_HAS_MSSQL) || defined(CPPLINQ_HAS_MYSQL) || defined(CPPLINQ_HAS_POSTGRES)

#include <cstring>
#include <sstream>
#include <stdexcept>
#include <algorithm>

namespace cpplinq {

using detail::odbc::get_odbc_error;
using detail::odbc::check_rc;

// ============================================================================
// OdbcDataReader
// ============================================================================

OdbcDataReader::OdbcDataReader(SQLHSTMT hstmt, bool owns_stmt)
    : hstmt_(hstmt)
    , owns_stmt_(owns_stmt)
{
    if (hstmt_ != SQL_NULL_HSTMT) {
        SQLNumResultCols(hstmt_, &col_count_);
    }
}

OdbcDataReader::~OdbcDataReader() {
    if (owns_stmt_ && hstmt_ != SQL_NULL_HSTMT) {
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt_);
        hstmt_ = SQL_NULL_HSTMT;
    }
}

void OdbcDataReader::fetch_row_cache() {
    row_cache_.assign(col_count_, CachedCol{});
    for (SQLSMALLINT i = 1; i <= col_count_; ++i) {
        auto& col = row_cache_[i - 1];

        SQLCHAR col_name[256];
        SQLSMALLINT name_len = 0;
        SQLSMALLINT data_type = 0;
        SQLULEN col_size = 0;
        SQLSMALLINT dec_digits = 0;
        SQLSMALLINT nullable = 0;

        SQLDescribeColA(hstmt_, i, col_name, sizeof(col_name), &name_len, &data_type, &col_size, &dec_digits, &nullable);

        switch (data_type) {
            case SQL_BIGINT:
            case SQL_INTEGER:
            case SQL_SMALLINT:
            case SQL_TINYINT: {
                int64_t val = 0;
                SQLLEN ind = 0;
                SQLRETURN rc = SQLGetData(hstmt_, i, SQL_C_SBIGINT, &val, sizeof(val), &ind);
                if (SQL_SUCCEEDED(rc) && ind != SQL_NULL_DATA) {
                    col.is_null = false;
                    col.int_val = val;
                    col.double_val = static_cast<double>(val);
                    col.str_val = std::to_string(val);
                    col.bool_val = (val != 0);
                }
                break;
            }
            case SQL_FLOAT:
            case SQL_REAL:
            case SQL_DOUBLE:
            case SQL_DECIMAL:
            case SQL_NUMERIC: {
                double val = 0.0;
                SQLLEN ind = 0;
                SQLRETURN rc = SQLGetData(hstmt_, i, SQL_C_DOUBLE, &val, sizeof(val), &ind);
                if (SQL_SUCCEEDED(rc) && ind != SQL_NULL_DATA) {
                    col.is_null = false;
                    col.double_val = val;
                    col.int_val = static_cast<int64_t>(val);
                    col.str_val = std::to_string(val);
                    col.bool_val = (val != 0.0);
                }
                break;
            }
            case SQL_BIT: {
                unsigned char val = 0;
                SQLLEN ind = 0;
                SQLRETURN rc = SQLGetData(hstmt_, i, SQL_C_BIT, &val, sizeof(val), &ind);
                if (SQL_SUCCEEDED(rc) && ind != SQL_NULL_DATA) {
                    col.is_null = false;
                    col.bool_val = (val != 0);
                    col.int_val = val ? 1 : 0;
                    col.double_val = val ? 1.0 : 0.0;
                    col.str_val = val ? "1" : "0";
                }
                break;
            }
            case SQL_BINARY:
            case SQL_VARBINARY:
            case SQL_LONGVARBINARY: {
                std::vector<uint8_t> buffer(4096);
                SQLLEN ind = 0;
                SQLRETURN rc = SQLGetData(hstmt_, i, SQL_C_BINARY, buffer.data(), buffer.size(), &ind);
                if (SQL_SUCCEEDED(rc) && ind != SQL_NULL_DATA) {
                    col.is_null = false;
                    if (ind >= 0 && ind <= static_cast<SQLLEN>(buffer.size())) {
                        buffer.resize(static_cast<size_t>(ind));
                        col.blob_val = std::move(buffer);
                    } else if (ind > static_cast<SQLLEN>(buffer.size())) {
                        size_t total_size = static_cast<size_t>(ind);
                        col.blob_val = std::move(buffer);
                        size_t offset = col.blob_val.size();
                        col.blob_val.resize(total_size);
                        SQLLEN ind2 = 0;
                        rc = SQLGetData(hstmt_, i, SQL_C_BINARY, col.blob_val.data() + offset, total_size - offset, &ind2);
                        if (!SQL_SUCCEEDED(rc)) {
                            col.blob_val.resize(offset);
                        }
                    } else {
                        col.blob_val = std::move(buffer);
                    }
                }
                break;
            }
            default: {
                char buffer[4096];
                SQLLEN ind = 0;
                SQLRETURN rc = SQLGetData(hstmt_, i, SQL_C_CHAR, buffer, sizeof(buffer), &ind);
                if (SQL_SUCCEEDED(rc) && ind != SQL_NULL_DATA) {
                    col.is_null = false;
                    if (ind >= 0 && ind < static_cast<SQLLEN>(sizeof(buffer))) {
                        col.str_val = std::string(buffer, static_cast<size_t>(ind));
                    } else if (ind >= static_cast<SQLLEN>(sizeof(buffer))) {
                        col.str_val = std::string(buffer, sizeof(buffer) - 1);
                        SQLLEN remaining = ind - (sizeof(buffer) - 1);
                        std::vector<char> large_buf(static_cast<size_t>(remaining) + 1);
                        SQLLEN ind2 = 0;
                        rc = SQLGetData(hstmt_, i, SQL_C_CHAR, large_buf.data(), large_buf.size(), &ind2);
                        if (SQL_SUCCEEDED(rc)) {
                            col.str_val.append(large_buf.data());
                        }
                    } else {
                        col.str_val = std::string(buffer);
                    }
                    try { col.int_val = std::stoll(col.str_val); } catch (...) {}
                    try { col.double_val = std::stod(col.str_val); } catch (...) {}
                    col.bool_val = (col.str_val == "1" || col.str_val == "true" || col.str_val == "TRUE" || col.str_val == "t" || col.str_val == "T");
                }
                break;
            }
        }
    }
}

bool OdbcDataReader::next() {
    if (hstmt_ == SQL_NULL_HSTMT) return false;
    SQLRETURN rc = SQLFetch(hstmt_);
    if (rc == SQL_NO_DATA) {
        return false;
    }
    if (!SQL_SUCCEEDED(rc)) {
        check_rc(rc, SQL_HANDLE_STMT, hstmt_, "SQLFetch");
        return false;
    }
    fetch_row_cache();
    return true;
}

int OdbcDataReader::column_count() const {
    return static_cast<int>(col_count_);
}

bool OdbcDataReader::is_null(int col) const {
    if (col < 0 || col >= static_cast<int>(row_cache_.size())) return true;
    return row_cache_[col].is_null;
}

int64_t OdbcDataReader::get_int64(int col) const {
    if (col < 0 || col >= static_cast<int>(row_cache_.size()) || row_cache_[col].is_null) return 0;
    return row_cache_[col].int_val;
}

double OdbcDataReader::get_double(int col) const {
    if (col < 0 || col >= static_cast<int>(row_cache_.size()) || row_cache_[col].is_null) return 0.0;
    return row_cache_[col].double_val;
}

std::string OdbcDataReader::get_string(int col) const {
    if (col < 0 || col >= static_cast<int>(row_cache_.size()) || row_cache_[col].is_null) return {};
    return row_cache_[col].str_val;
}

bool OdbcDataReader::get_bool(int col) const {
    if (col < 0 || col >= static_cast<int>(row_cache_.size()) || row_cache_[col].is_null) return false;
    return row_cache_[col].bool_val;
}

std::vector<uint8_t> OdbcDataReader::get_blob(int col) const {
    if (col < 0 || col >= static_cast<int>(row_cache_.size()) || row_cache_[col].is_null) return {};
    return row_cache_[col].blob_val;
}

// ============================================================================
// OdbcPreparedStatement
// ============================================================================

OdbcPreparedStatement::OdbcPreparedStatement(SQLHDBC hdbc, std::string_view sql)
    : hdbc_(hdbc)
    , sql_(sql)
{
    SQLRETURN rc = SQLAllocHandle(SQL_HANDLE_STMT, hdbc_, &hstmt_);
    check_rc(rc, SQL_HANDLE_DBC, hdbc_, "SQLAllocHandle(SQL_HANDLE_STMT)");

    rc = SQLPrepareA(hstmt_, reinterpret_cast<SQLCHAR*>(const_cast<char*>(sql_.data())), static_cast<SQLINTEGER>(sql_.size()));
    check_rc(rc, SQL_HANDLE_STMT, hstmt_, "SQLPrepareA");
}

OdbcPreparedStatement::~OdbcPreparedStatement() {
    if (hstmt_ != SQL_NULL_HSTMT) {
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt_);
        hstmt_ = SQL_NULL_HSTMT;
    }
}

void OdbcPreparedStatement::bind(int index, const BoundValue& value) {
    if (index < 0) {
        throw DbException("Invalid parameter index: " + std::to_string(index));
    }
    if (static_cast<size_t>(index) >= params_.size()) {
        params_.resize(index + 1);
    }
    params_[index] = value;
}

void OdbcPreparedStatement::reset() {
    params_.clear();
    storage_.clear();
    if (hstmt_ != SQL_NULL_HSTMT) {
        SQLFreeStmt(hstmt_, SQL_RESET_PARAMS);
        SQLFreeStmt(hstmt_, SQL_CLOSE);
        SQLFreeStmt(hstmt_, SQL_UNBIND);
    }
}

void OdbcPreparedStatement::cancel() {
    if (hstmt_ != SQL_NULL_HSTMT) {
        SQLCancel(hstmt_);
    }
}

void OdbcPreparedStatement::set_timeout(uint32_t seconds) {
    if (hstmt_ != SQL_NULL_HSTMT) {
        SQLSetStmtAttr(hstmt_, SQL_ATTR_QUERY_TIMEOUT, reinterpret_cast<SQLPOINTER>(static_cast<uintptr_t>(seconds)), SQL_IS_UINTEGER);
    }
}

void OdbcPreparedStatement::set_stop_token(std::stop_token token) {
    stop_token_ = token;
    if (token.stop_possible() && hstmt_ != SQL_NULL_HSTMT) {
        stop_cb_.emplace(token, [this]() {
            cancel();
        });
    }
}

void OdbcPreparedStatement::apply_bindings() {
    SQLFreeStmt(hstmt_, SQL_RESET_PARAMS);
    storage_.resize(params_.size());

    for (size_t i = 0; i < params_.size(); ++i) {
        auto& store = storage_[i];
        const auto& p = params_[i];
        SQLUSMALLINT param_num = static_cast<SQLUSMALLINT>(i + 1);

        std::visit([&](const auto& val) {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                store.c_type = SQL_C_CHAR;
                store.sql_type = SQL_VARCHAR;
                store.col_size = 1;
                store.dec_digits = 0;
                store.ind = SQL_NULL_DATA;
                SQLRETURN rc = SQLBindParameter(hstmt_, param_num, SQL_PARAM_INPUT, store.c_type, store.sql_type,
                                                store.col_size, store.dec_digits, nullptr, 0, &store.ind);
                check_rc(rc, SQL_HANDLE_STMT, hstmt_, "SQLBindParameter(NULL)");
            } else if constexpr (std::is_same_v<T, int64_t>) {
                store.c_type = SQL_C_SBIGINT;
                store.sql_type = SQL_BIGINT;
                store.col_size = 19;
                store.dec_digits = 0;
                store.ind = sizeof(int64_t);
                store.buffer.resize(sizeof(int64_t));
                std::memcpy(store.buffer.data(), &val, sizeof(int64_t));
                SQLRETURN rc = SQLBindParameter(hstmt_, param_num, SQL_PARAM_INPUT, store.c_type, store.sql_type,
                                                store.col_size, store.dec_digits, store.buffer.data(), sizeof(int64_t), &store.ind);
                check_rc(rc, SQL_HANDLE_STMT, hstmt_, "SQLBindParameter(int64)");
            } else if constexpr (std::is_same_v<T, double>) {
                store.c_type = SQL_C_DOUBLE;
                store.sql_type = SQL_DOUBLE;
                store.col_size = 53;
                store.dec_digits = 0;
                store.ind = sizeof(double);
                store.buffer.resize(sizeof(double));
                std::memcpy(store.buffer.data(), &val, sizeof(double));
                SQLRETURN rc = SQLBindParameter(hstmt_, param_num, SQL_PARAM_INPUT, store.c_type, store.sql_type,
                                                store.col_size, store.dec_digits, store.buffer.data(), sizeof(double), &store.ind);
                check_rc(rc, SQL_HANDLE_STMT, hstmt_, "SQLBindParameter(double)");
            } else if constexpr (std::is_same_v<T, std::string>) {
                store.c_type = SQL_C_CHAR;
                store.sql_type = SQL_VARCHAR;
                store.col_size = val.empty() ? 1 : static_cast<SQLULEN>(val.size());
                store.dec_digits = 0;
                store.ind = static_cast<SQLLEN>(val.size());
                store.buffer.resize(val.size() + 1);
                std::memcpy(store.buffer.data(), val.c_str(), val.size() + 1);
                SQLRETURN rc = SQLBindParameter(hstmt_, param_num, SQL_PARAM_INPUT, store.c_type, store.sql_type,
                                                store.col_size, store.dec_digits, store.buffer.data(), static_cast<SQLLEN>(store.buffer.size()), &store.ind);
                check_rc(rc, SQL_HANDLE_STMT, hstmt_, "SQLBindParameter(string)");
            } else if constexpr (std::is_same_v<T, bool>) {
                store.c_type = SQL_C_BIT;
                store.sql_type = SQL_BIT;
                store.col_size = 1;
                store.dec_digits = 0;
                store.ind = 1;
                store.buffer.resize(1);
                store.buffer[0] = val ? 1 : 0;
                SQLRETURN rc = SQLBindParameter(hstmt_, param_num, SQL_PARAM_INPUT, store.c_type, store.sql_type,
                                                store.col_size, store.dec_digits, store.buffer.data(), 1, &store.ind);
                check_rc(rc, SQL_HANDLE_STMT, hstmt_, "SQLBindParameter(bool)");
            } else if constexpr (std::is_same_v<T, std::vector<uint8_t>>) {
                store.c_type = SQL_C_BINARY;
                store.sql_type = SQL_VARBINARY;
                store.col_size = val.empty() ? 1 : static_cast<SQLULEN>(val.size());
                store.dec_digits = 0;
                store.ind = static_cast<SQLLEN>(val.size());
                store.buffer = val;
                SQLRETURN rc = SQLBindParameter(hstmt_, param_num, SQL_PARAM_INPUT, store.c_type, store.sql_type,
                                                store.col_size, store.dec_digits, store.buffer.data(), static_cast<SQLLEN>(store.buffer.size()), &store.ind);
                check_rc(rc, SQL_HANDLE_STMT, hstmt_, "SQLBindParameter(blob)");
            }
        }, p);
    }
}

std::unique_ptr<IDataReader> OdbcPreparedStatement::execute_query() {
    apply_bindings();
    SQLRETURN rc = SQLExecute(hstmt_);
    if (!SQL_SUCCEEDED(rc) && rc != SQL_NO_DATA) {
        check_rc(rc, SQL_HANDLE_STMT, hstmt_, "SQLExecute(execute_query)");
    }
    return std::make_unique<OdbcDataReader>(hstmt_, false);
}

size_t OdbcPreparedStatement::execute_non_query() {
    apply_bindings();
    SQLRETURN rc = SQLExecute(hstmt_);
    if (!SQL_SUCCEEDED(rc) && rc != SQL_NO_DATA) {
        check_rc(rc, SQL_HANDLE_STMT, hstmt_, "SQLExecute(execute_non_query)");
    }
    SQLLEN row_count = 0;
    SQLRowCount(hstmt_, &row_count);
    return row_count > 0 ? static_cast<size_t>(row_count) : 0;
}

// ============================================================================
// OdbcConnection
// ============================================================================

OdbcConnection::OdbcConnection(std::string connection_string)
    : connection_string_(std::move(connection_string))
{}

OdbcConnection::~OdbcConnection() {
    try {
        close();
    } catch (...) {}
}

void OdbcConnection::open() {
    if (is_open_) return;

    SQLRETURN rc = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv_);
    check_rc(rc, SQL_HANDLE_ENV, henv_, "SQLAllocHandle(SQL_HANDLE_ENV)");

    rc = SQLSetEnvAttr(henv_, SQL_ATTR_ODBC_VERSION, reinterpret_cast<SQLPOINTER>(SQL_OV_ODBC3), 0);
    check_rc(rc, SQL_HANDLE_ENV, henv_, "SQLSetEnvAttr(SQL_ATTR_ODBC_VERSION)");

    rc = SQLAllocHandle(SQL_HANDLE_DBC, henv_, &hdbc_);
    check_rc(rc, SQL_HANDLE_ENV, henv_, "SQLAllocHandle(SQL_HANDLE_DBC)");

    std::vector<std::string> candidates = get_connection_candidates(connection_string_);
    std::string last_err;
    bool connected = false;

    for (const auto& conn_in : candidates) {
        SQLCHAR conn_out[1024];
        SQLSMALLINT out_len = 0;
        rc = SQLDriverConnectA(
            hdbc_,
            nullptr,
            reinterpret_cast<SQLCHAR*>(const_cast<char*>(conn_in.data())),
            static_cast<SQLSMALLINT>(conn_in.size()),
            conn_out,
            sizeof(conn_out),
            &out_len,
            SQL_DRIVER_NOPROMPT
        );

        if (SQL_SUCCEEDED(rc)) {
            connected = true;
            break;
        } else {
            last_err = get_odbc_error(SQL_HANDLE_DBC, hdbc_);
        }
    }

    if (!connected) {
        close();
        throw DbException("Failed to connect to " + get_driver_display_name() + " (" + connection_string_ + "): " + last_err);
    }

    is_open_ = true;
}

void OdbcConnection::close() {
    if (hdbc_ != SQL_NULL_HDBC) {
        if (is_open_) {
            SQLDisconnect(hdbc_);
            is_open_ = false;
        }
        SQLFreeHandle(SQL_HANDLE_DBC, hdbc_);
        hdbc_ = SQL_NULL_HDBC;
    }
    if (henv_ != SQL_NULL_HENV) {
        SQLFreeHandle(SQL_HANDLE_ENV, henv_);
        henv_ = SQL_NULL_HENV;
    }
}

bool OdbcConnection::is_open() const {
    return is_open_;
}

std::unique_ptr<IPreparedStatement> OdbcConnection::prepare(std::string_view sql) {
    if (!is_open_) {
        throw DbException("Cannot prepare statement: " + get_driver_display_name() + " connection is not open");
    }
    return std::make_unique<OdbcPreparedStatement>(hdbc_, sql);
}

void OdbcConnection::execute(std::string_view sql) {
    if (!is_open_) {
        throw DbException("Cannot execute statement: " + get_driver_display_name() + " connection is not open");
    }
    SQLHSTMT hstmt = SQL_NULL_HSTMT;
    SQLRETURN rc = SQLAllocHandle(SQL_HANDLE_STMT, hdbc_, &hstmt);
    check_rc(rc, SQL_HANDLE_DBC, hdbc_, "SQLAllocHandle(SQL_HANDLE_STMT)");

    rc = SQLExecDirectA(hstmt, reinterpret_cast<SQLCHAR*>(const_cast<char*>(sql.data())), static_cast<SQLINTEGER>(sql.size()));
    if (!SQL_SUCCEEDED(rc) && rc != SQL_NO_DATA) {
        std::string err = get_odbc_error(SQL_HANDLE_STMT, hstmt);
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        throw DbException(get_driver_display_name() + " execute failed: " + err);
    }
    SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
}

void OdbcConnection::begin_transaction() {
    if (!is_open_) throw DbException(get_driver_display_name() + " connection is not open");
    SQLRETURN rc = SQLSetConnectAttr(hdbc_, SQL_ATTR_AUTOCOMMIT, reinterpret_cast<SQLPOINTER>(SQL_AUTOCOMMIT_OFF), SQL_IS_UINTEGER);
    check_rc(rc, SQL_HANDLE_DBC, hdbc_, "SQLSetConnectAttr(SQL_AUTOCOMMIT_OFF)");
}

void OdbcConnection::commit() {
    if (!is_open_) throw DbException(get_driver_display_name() + " connection is not open");
    SQLRETURN rc = SQLEndTran(SQL_HANDLE_DBC, hdbc_, SQL_COMMIT);
    check_rc(rc, SQL_HANDLE_DBC, hdbc_, "SQLEndTran(SQL_COMMIT)");
    SQLSetConnectAttr(hdbc_, SQL_ATTR_AUTOCOMMIT, reinterpret_cast<SQLPOINTER>(SQL_AUTOCOMMIT_ON), SQL_IS_UINTEGER);
}

void OdbcConnection::rollback() {
    if (!is_open_) throw DbException(get_driver_display_name() + " connection is not open");
    SQLRETURN rc = SQLEndTran(SQL_HANDLE_DBC, hdbc_, SQL_ROLLBACK);
    check_rc(rc, SQL_HANDLE_DBC, hdbc_, "SQLEndTran(SQL_ROLLBACK)");
    SQLSetConnectAttr(hdbc_, SQL_ATTR_AUTOCOMMIT, reinterpret_cast<SQLPOINTER>(SQL_AUTOCOMMIT_ON), SQL_IS_UINTEGER);
}

DriverInfo OdbcConnection::info() const {
    DriverInfo i = get_default_driver_info();
    if (hdbc_ != SQL_NULL_HDBC) {
        char buf[256] = {0};
        SQLSMALLINT len = 0;
        if (SQL_SUCCEEDED(SQLGetInfoA(hdbc_, SQL_DRIVER_NAME, buf, sizeof(buf), &len))) {
            i.driver_name = buf;
        }
        if (SQL_SUCCEEDED(SQLGetInfoA(hdbc_, SQL_DRIVER_VER, buf, sizeof(buf), &len))) {
            i.driver_version = buf;
        }
        if (SQL_SUCCEEDED(SQLGetInfoA(hdbc_, SQL_DBMS_NAME, buf, sizeof(buf), &len))) {
            i.dbms_name = buf;
        }
        if (SQL_SUCCEEDED(SQLGetInfoA(hdbc_, SQL_DBMS_VER, buf, sizeof(buf), &len))) {
            i.dbms_version = buf;
        }
        if (SQL_SUCCEEDED(SQLGetInfoA(hdbc_, SQL_ODBC_VER, buf, sizeof(buf), &len))) {
            i.odbc_version = buf;
        }
    }
    return i;
}

size_t OdbcConnection::insert_many_batch(
    std::string_view sql,
    const std::vector<BoundValue>& flat_params,
    size_t col_count,
    size_t row_count
) {
    return detail::odbc::execute_insert_many_batch(
        hdbc_, sql, flat_params, col_count, row_count, get_driver_display_name().c_str()
    );
}

} // namespace cpplinq

#endif // defined(CPPLINQ_HAS_MSSQL) || defined(CPPLINQ_HAS_MYSQL) || defined(CPPLINQ_HAS_POSTGRES)
