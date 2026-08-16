#include "driver/mysql_connection.h"

#ifdef CPPLINQ_HAS_MYSQL

#include <cstring>
#include <sstream>
#include <stdexcept>

namespace cpplinq {

namespace {

std::string get_odbc_error(SQLSMALLINT handle_type, SQLHANDLE handle) {
    if (handle == SQL_NULL_HANDLE) return "Unknown ODBC error";
    SQLCHAR sqlstate[6] = {0};
    SQLINTEGER native_error = 0;
    SQLCHAR message[1024] = {0};
    SQLSMALLINT text_length = 0;
    std::string full_msg;

    SQLSMALLINT i = 1;
    while (SQLGetDiagRecA(handle_type, handle, i++, sqlstate, &native_error, message, sizeof(message), &text_length) == SQL_SUCCESS) {
        if (!full_msg.empty()) full_msg += "; ";
        full_msg += "[" + std::string(reinterpret_cast<char*>(sqlstate)) + "] " + std::string(reinterpret_cast<char*>(message));
    }
    return full_msg.empty() ? "ODBC error (no diagnostic info)" : full_msg;
}

void check_rc(SQLRETURN rc, SQLSMALLINT handle_type, SQLHANDLE handle, const char* context) {
    if (!SQL_SUCCEEDED(rc) && rc != SQL_NO_DATA) {
        std::string err = get_odbc_error(handle_type, handle);
        throw DbException(std::string(context) + ": " + err);
    }
}

} // namespace

// ----------------------------------------------------------------------------
// MysqlDataReader
// ----------------------------------------------------------------------------

MysqlDataReader::MysqlDataReader(SQLHSTMT hstmt, bool owns_stmt)
    : hstmt_(hstmt)
    , owns_stmt_(owns_stmt)
{
    if (hstmt_ != SQL_NULL_HSTMT) {
        SQLNumResultCols(hstmt_, &col_count_);
    }
}

MysqlDataReader::~MysqlDataReader() {
    if (owns_stmt_ && hstmt_ != SQL_NULL_HSTMT) {
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt_);
        hstmt_ = SQL_NULL_HSTMT;
    }
}

void MysqlDataReader::fetch_row_cache() {
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
                    col.int_val = val;
                    col.double_val = val;
                    col.str_val = val ? "1" : "0";
                }
                break;
            }
            case SQL_BINARY:
            case SQL_VARBINARY:
            case SQL_LONGVARBINARY: {
                SQLLEN ind = 0;
                char buf[1024];
                SQLRETURN rc = SQLGetData(hstmt_, i, SQL_C_BINARY, buf, sizeof(buf), &ind);
                if (SQL_SUCCEEDED(rc) && ind != SQL_NULL_DATA) {
                    col.is_null = false;
                    if (ind > 0) {
                        col.blob_val.assign(reinterpret_cast<uint8_t*>(buf), reinterpret_cast<uint8_t*>(buf) + (ind < (SQLLEN)sizeof(buf) ? ind : sizeof(buf)));
                    }
                }
                break;
            }
            default: {
                // Treat as string
                char buf[4096];
                SQLLEN ind = 0;
                SQLRETURN rc = SQLGetData(hstmt_, i, SQL_C_CHAR, buf, sizeof(buf), &ind);
                if (SQL_SUCCEEDED(rc) && ind != SQL_NULL_DATA) {
                    col.is_null = false;
                    col.str_val = buf;
                    try {
                        col.int_val = std::stoll(col.str_val);
                    } catch (...) {}
                    try {
                        col.double_val = std::stod(col.str_val);
                    } catch (...) {}
                    col.bool_val = (!col.str_val.empty() && col.str_val != "0" && col.str_val != "false");
                }
                break;
            }
        }
    }
}

bool MysqlDataReader::next() {
    if (hstmt_ == SQL_NULL_HSTMT) return false;
    SQLRETURN rc = SQLFetch(hstmt_);
    if (rc == SQL_NO_DATA) {
        return false;
    }
    check_rc(rc, SQL_HANDLE_STMT, hstmt_, "SQLFetch");
    fetch_row_cache();
    return true;
}

int MysqlDataReader::column_count() const {
    return col_count_;
}

bool MysqlDataReader::is_null(int col) const {
    if (col < 0 || col >= static_cast<int>(row_cache_.size())) return true;
    return row_cache_[col].is_null;
}

int64_t MysqlDataReader::get_int64(int col) const {
    if (is_null(col)) return 0;
    return row_cache_[col].int_val;
}

double MysqlDataReader::get_double(int col) const {
    if (is_null(col)) return 0.0;
    return row_cache_[col].double_val;
}

std::string MysqlDataReader::get_string(int col) const {
    if (is_null(col)) return "";
    return row_cache_[col].str_val;
}

bool MysqlDataReader::get_bool(int col) const {
    if (is_null(col)) return false;
    return row_cache_[col].bool_val;
}

std::vector<uint8_t> MysqlDataReader::get_blob(int col) const {
    if (is_null(col)) return {};
    return row_cache_[col].blob_val;
}

// ----------------------------------------------------------------------------
// MysqlPreparedStatement
// ----------------------------------------------------------------------------

MysqlPreparedStatement::MysqlPreparedStatement(SQLHDBC hdbc, std::string_view sql)
    : hdbc_(hdbc)
    , sql_(sql)
{
    SQLRETURN rc = SQLAllocHandle(SQL_HANDLE_STMT, hdbc_, &hstmt_);
    check_rc(rc, SQL_HANDLE_DBC, hdbc_, "SQLAllocHandle(SQL_HANDLE_STMT)");

    rc = SQLPrepareA(hstmt_, reinterpret_cast<SQLCHAR*>(const_cast<char*>(sql_.data())), static_cast<SQLINTEGER>(sql_.size()));
    check_rc(rc, SQL_HANDLE_STMT, hstmt_, "SQLPrepareA");
}

MysqlPreparedStatement::~MysqlPreparedStatement() {
    if (hstmt_ != SQL_NULL_HSTMT) {
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt_);
        hstmt_ = SQL_NULL_HSTMT;
    }
}

void MysqlPreparedStatement::bind(int index, const BoundValue& value) {
    if (index < 0) {
        throw DbException("Invalid parameter index: " + std::to_string(index));
    }
    if (static_cast<size_t>(index) >= params_.size()) {
        params_.resize(index + 1, std::monostate{});
    }
    params_[index] = value;
}

void MysqlPreparedStatement::apply_bindings() {
    storage_.resize(params_.size());
    for (size_t i = 0; i < params_.size(); ++i) {
        auto& p = storage_[i];
        SQLUSMALLINT param_num = static_cast<SQLUSMALLINT>(i + 1);

        std::visit([&](const auto& val) {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                p.ind = SQL_NULL_DATA;
                p.c_type = SQL_C_DEFAULT;
                p.sql_type = SQL_VARCHAR;
                p.col_size = 1;
                p.dec_digits = 0;
                SQLBindParameter(hstmt_, param_num, SQL_PARAM_INPUT, p.c_type, p.sql_type, p.col_size, p.dec_digits, nullptr, 0, &p.ind);
            } else if constexpr (std::is_same_v<T, int64_t>) {
                p.buffer.resize(sizeof(int64_t));
                std::memcpy(p.buffer.data(), &val, sizeof(int64_t));
                p.ind = sizeof(int64_t);
                p.c_type = SQL_C_SBIGINT;
                p.sql_type = SQL_BIGINT;
                p.col_size = 0;
                p.dec_digits = 0;
                SQLBindParameter(hstmt_, param_num, SQL_PARAM_INPUT, p.c_type, p.sql_type, p.col_size, p.dec_digits, p.buffer.data(), sizeof(int64_t), &p.ind);
            } else if constexpr (std::is_same_v<T, double>) {
                p.buffer.resize(sizeof(double));
                std::memcpy(p.buffer.data(), &val, sizeof(double));
                p.ind = sizeof(double);
                p.c_type = SQL_C_DOUBLE;
                p.sql_type = SQL_DOUBLE;
                p.col_size = 15;
                p.dec_digits = 0;
                SQLBindParameter(hstmt_, param_num, SQL_PARAM_INPUT, p.c_type, p.sql_type, p.col_size, p.dec_digits, p.buffer.data(), sizeof(double), &p.ind);
            } else if constexpr (std::is_same_v<T, std::string>) {
                p.buffer.assign(val.begin(), val.end());
                p.buffer.push_back('\0');
                p.ind = static_cast<SQLLEN>(val.size());
                p.c_type = SQL_C_CHAR;
                p.sql_type = SQL_VARCHAR;
                p.col_size = static_cast<SQLULEN>(val.size() > 0 ? val.size() : 1);
                p.dec_digits = 0;
                SQLBindParameter(hstmt_, param_num, SQL_PARAM_INPUT, p.c_type, p.sql_type, p.col_size, p.dec_digits, p.buffer.data(), static_cast<SQLLEN>(p.buffer.size()), &p.ind);
            } else if constexpr (std::is_same_v<T, bool>) {
                unsigned char b = val ? 1 : 0;
                p.buffer.resize(1);
                p.buffer[0] = b;
                p.ind = 1;
                p.c_type = SQL_C_BIT;
                p.sql_type = SQL_BIT;
                p.col_size = 1;
                p.dec_digits = 0;
                SQLBindParameter(hstmt_, param_num, SQL_PARAM_INPUT, p.c_type, p.sql_type, p.col_size, p.dec_digits, p.buffer.data(), 1, &p.ind);
            } else if constexpr (std::is_same_v<T, std::vector<uint8_t>>) {
                p.buffer = val;
                p.ind = static_cast<SQLLEN>(val.size());
                p.c_type = SQL_C_BINARY;
                p.sql_type = SQL_LONGVARBINARY;
                p.col_size = static_cast<SQLULEN>(val.size() > 0 ? val.size() : 1);
                p.dec_digits = 0;
                SQLBindParameter(hstmt_, param_num, SQL_PARAM_INPUT, p.c_type, p.sql_type, p.col_size, p.dec_digits, p.buffer.data(), static_cast<SQLLEN>(val.size()), &p.ind);
            }
        }, params_[i]);
    }
}

std::unique_ptr<IDataReader> MysqlPreparedStatement::execute_query() {
    apply_bindings();
    SQLRETURN rc = SQLExecute(hstmt_);
    check_rc(rc, SQL_HANDLE_STMT, hstmt_, "SQLExecute");
    return std::make_unique<MysqlDataReader>(hstmt_, false);
}

size_t MysqlPreparedStatement::execute_non_query() {
    apply_bindings();
    SQLRETURN rc = SQLExecute(hstmt_);
    check_rc(rc, SQL_HANDLE_STMT, hstmt_, "SQLExecute");

    SQLLEN row_count = 0;
    SQLRowCount(hstmt_, &row_count);
    return row_count > 0 ? static_cast<size_t>(row_count) : 0;
}

void MysqlPreparedStatement::reset() {
    params_.clear();
    storage_.clear();
    if (hstmt_ != SQL_NULL_HSTMT) {
        SQLFreeStmt(hstmt_, SQL_RESET_PARAMS);
        SQLFreeStmt(hstmt_, SQL_CLOSE);
    }
}

// ----------------------------------------------------------------------------
// MysqlConnection
// ----------------------------------------------------------------------------

MysqlConnection::MysqlConnection(std::string connection_string)
    : conn_str_(std::move(connection_string))
{}

MysqlConnection::~MysqlConnection() {
    try {
        close();
    } catch (...) {}
}

void MysqlConnection::open() {
    if (is_open_) return;

    SQLRETURN rc = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv_);
    check_rc(rc, SQL_HANDLE_ENV, henv_, "SQLAllocHandle(SQL_HANDLE_ENV)");

    rc = SQLSetEnvAttr(henv_, SQL_ATTR_ODBC_VERSION, reinterpret_cast<SQLPOINTER>(SQL_OV_ODBC3), 0);
    check_rc(rc, SQL_HANDLE_ENV, henv_, "SQLSetEnvAttr(SQL_OV_ODBC3)");

    rc = SQLAllocHandle(SQL_HANDLE_DBC, henv_, &hdbc_);
    check_rc(rc, SQL_HANDLE_ENV, henv_, "SQLAllocHandle(SQL_HANDLE_DBC)");

    // Determine connection string candidates (direct DSN vs connection string)
    std::string trimmed = conn_str_;
    while (!trimmed.empty() && (trimmed.front() == ' ' || trimmed.front() == '\t')) trimmed.erase(trimmed.begin());
    while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\t')) trimmed.pop_back();

    std::vector<std::string> candidates;
    if (trimmed.find('=') == std::string::npos) {
        // Plain DSN name
        candidates.push_back("DSN=" + trimmed + ";");
        candidates.push_back(trimmed);
    } else {
        candidates.push_back(trimmed);
    }

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
        throw DbException("Failed to connect to MySQL Server (" + conn_str_ + "): " + last_err);
    }

    is_open_ = true;
}

void MysqlConnection::close() {
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

bool MysqlConnection::is_open() const {
    return is_open_;
}

std::unique_ptr<IPreparedStatement> MysqlConnection::prepare(std::string_view sql) {
    if (!is_open_) {
        throw DbException("Cannot prepare statement: MySQL connection is not open");
    }
    return std::make_unique<MysqlPreparedStatement>(hdbc_, sql);
}

void MysqlConnection::execute(std::string_view sql) {
    if (!is_open_) {
        throw DbException("Cannot execute statement: MySQL connection is not open");
    }
    SQLHSTMT hstmt = SQL_NULL_HSTMT;
    SQLRETURN rc = SQLAllocHandle(SQL_HANDLE_STMT, hdbc_, &hstmt);
    check_rc(rc, SQL_HANDLE_DBC, hdbc_, "SQLAllocHandle(SQL_HANDLE_STMT)");

    rc = SQLExecDirectA(hstmt, reinterpret_cast<SQLCHAR*>(const_cast<char*>(sql.data())), static_cast<SQLINTEGER>(sql.size()));
    if (!SQL_SUCCEEDED(rc) && rc != SQL_NO_DATA) {
        std::string err = get_odbc_error(SQL_HANDLE_STMT, hstmt);
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        throw DbException("MySQL execute failed: " + err);
    }
    SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
}

void MysqlConnection::begin_transaction() {
    if (!is_open_) throw DbException("MySQL connection is not open");
    SQLRETURN rc = SQLSetConnectAttr(hdbc_, SQL_ATTR_AUTOCOMMIT, reinterpret_cast<SQLPOINTER>(SQL_AUTOCOMMIT_OFF), SQL_IS_UINTEGER);
    check_rc(rc, SQL_HANDLE_DBC, hdbc_, "SQLSetConnectAttr(SQL_AUTOCOMMIT_OFF)");
}

void MysqlConnection::commit() {
    if (!is_open_) throw DbException("MySQL connection is not open");
    SQLRETURN rc = SQLEndTran(SQL_HANDLE_DBC, hdbc_, SQL_COMMIT);
    check_rc(rc, SQL_HANDLE_DBC, hdbc_, "SQLEndTran(SQL_COMMIT)");
    SQLSetConnectAttr(hdbc_, SQL_ATTR_AUTOCOMMIT, reinterpret_cast<SQLPOINTER>(SQL_AUTOCOMMIT_ON), SQL_IS_UINTEGER);
}

void MysqlConnection::rollback() {
    if (!is_open_) throw DbException("MySQL connection is not open");
    SQLRETURN rc = SQLEndTran(SQL_HANDLE_DBC, hdbc_, SQL_ROLLBACK);
    check_rc(rc, SQL_HANDLE_DBC, hdbc_, "SQLEndTran(SQL_ROLLBACK)");
    SQLSetConnectAttr(hdbc_, SQL_ATTR_AUTOCOMMIT, reinterpret_cast<SQLPOINTER>(SQL_AUTOCOMMIT_ON), SQL_IS_UINTEGER);
}

const ISqlDialect& MysqlConnection::dialect() const {
    return dialect_;
}

// ----------------------------------------------------------------------------
// make_connection<mysql> Specialization
// ----------------------------------------------------------------------------

template <>
std::unique_ptr<IConnection> make_connection<mysql>(const std::string& connection_string) {
    return std::make_unique<MysqlConnection>(connection_string);
}

} // namespace cpplinq

#endif // CPPLINQ_HAS_MYSQL
