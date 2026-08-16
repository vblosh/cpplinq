#include "driver/mssql_connection.h"

#ifdef CPPLINQ_HAS_MSSQL

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
// MssqlDataReader
// ----------------------------------------------------------------------------

MssqlDataReader::MssqlDataReader(SQLHSTMT hstmt, bool owns_stmt)
    : hstmt_(hstmt)
    , owns_stmt_(owns_stmt)
{
    if (hstmt_ != SQL_NULL_HSTMT) {
        SQLNumResultCols(hstmt_, &col_count_);
    }
}

MssqlDataReader::~MssqlDataReader() {
    if (owns_stmt_ && hstmt_ != SQL_NULL_HSTMT) {
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt_);
        hstmt_ = SQL_NULL_HSTMT;
    }
}

void MssqlDataReader::fetch_row_cache() {
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
                    if (ind >= 0) {
                        buffer.resize(static_cast<size_t>(ind));
                    }
                    col.blob_val = std::move(buffer);
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
                        rc = SQLGetData(hstmt_, i, SQL_C_CHAR, large_buf.data(), large_buf.size(), &ind);
                        if (SQL_SUCCEEDED(rc)) {
                            col.str_val.append(large_buf.data());
                        }
                    } else {
                        col.str_val = std::string(buffer);
                    }
                    try { col.int_val = std::stoll(col.str_val); } catch (...) {}
                    try { col.double_val = std::stod(col.str_val); } catch (...) {}
                    col.bool_val = (col.str_val == "1" || col.str_val == "true" || col.str_val == "TRUE");
                }
                break;
            }
        }
    }
}

bool MssqlDataReader::next() {
    if (hstmt_ == SQL_NULL_HSTMT) return false;
    SQLRETURN rc = SQLFetch(hstmt_);
    if (SQL_SUCCEEDED(rc) && rc != SQL_NO_DATA) {
        fetch_row_cache();
        return true;
    }
    return false;
}

int MssqlDataReader::column_count() const {
    return static_cast<int>(col_count_);
}

bool MssqlDataReader::is_null(int col) const {
    if (col < 0 || col >= static_cast<int>(row_cache_.size())) return true;
    return row_cache_[col].is_null;
}

int64_t MssqlDataReader::get_int64(int col) const {
    if (col < 0 || col >= static_cast<int>(row_cache_.size()) || row_cache_[col].is_null) return 0;
    return row_cache_[col].int_val;
}

double MssqlDataReader::get_double(int col) const {
    if (col < 0 || col >= static_cast<int>(row_cache_.size()) || row_cache_[col].is_null) return 0.0;
    return row_cache_[col].double_val;
}

std::string MssqlDataReader::get_string(int col) const {
    if (col < 0 || col >= static_cast<int>(row_cache_.size()) || row_cache_[col].is_null) return {};
    return row_cache_[col].str_val;
}

bool MssqlDataReader::get_bool(int col) const {
    if (col < 0 || col >= static_cast<int>(row_cache_.size()) || row_cache_[col].is_null) return false;
    return row_cache_[col].bool_val;
}

std::vector<uint8_t> MssqlDataReader::get_blob(int col) const {
    if (col < 0 || col >= static_cast<int>(row_cache_.size()) || row_cache_[col].is_null) return {};
    return row_cache_[col].blob_val;
}

// ----------------------------------------------------------------------------
// MssqlPreparedStatement
// ----------------------------------------------------------------------------

MssqlPreparedStatement::MssqlPreparedStatement(SQLHDBC hdbc, std::string_view sql)
    : hdbc_(hdbc)
    , sql_(sql)
{
    SQLRETURN rc = SQLAllocHandle(SQL_HANDLE_STMT, hdbc_, &hstmt_);
    check_rc(rc, SQL_HANDLE_DBC, hdbc_, "SQLAllocHandle(SQL_HANDLE_STMT)");

    rc = SQLPrepareA(hstmt_, reinterpret_cast<SQLCHAR*>(sql_.data()), static_cast<SQLINTEGER>(sql_.size()));
    check_rc(rc, SQL_HANDLE_STMT, hstmt_, "SQLPrepareA");
}

MssqlPreparedStatement::~MssqlPreparedStatement() {
    if (hstmt_ != SQL_NULL_HSTMT) {
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt_);
        hstmt_ = SQL_NULL_HSTMT;
    }
}

void MssqlPreparedStatement::bind(int index, const BoundValue& value) {
    if (index < 0) {
        throw DbException("Invalid parameter index: " + std::to_string(index));
    }
    if (static_cast<size_t>(index) >= params_.size()) {
        params_.resize(index + 1);
    }
    params_[index] = value;
}

void MssqlPreparedStatement::reset() {
    params_.clear();
    storage_.clear();
    if (hstmt_ != SQL_NULL_HSTMT) {
        SQLFreeStmt(hstmt_, SQL_RESET_PARAMS);
        SQLFreeStmt(hstmt_, SQL_CLOSE);
    }
}

void MssqlPreparedStatement::apply_bindings() {
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
                SQLBindParameter(hstmt_, param_num, SQL_PARAM_INPUT, store.c_type, store.sql_type,
                                 store.col_size, store.dec_digits, nullptr, 0, &store.ind);
            } else if constexpr (std::is_same_v<T, int64_t>) {
                store.c_type = SQL_C_SBIGINT;
                store.sql_type = SQL_BIGINT;
                store.col_size = 0;
                store.dec_digits = 0;
                store.ind = sizeof(int64_t);
                store.buffer.resize(sizeof(int64_t));
                std::memcpy(store.buffer.data(), &val, sizeof(int64_t));
                SQLBindParameter(hstmt_, param_num, SQL_PARAM_INPUT, store.c_type, store.sql_type,
                                 store.col_size, store.dec_digits, store.buffer.data(), sizeof(int64_t), &store.ind);
            } else if constexpr (std::is_same_v<T, double>) {
                store.c_type = SQL_C_DOUBLE;
                store.sql_type = SQL_DOUBLE;
                store.col_size = 53;
                store.dec_digits = 0;
                store.ind = sizeof(double);
                store.buffer.resize(sizeof(double));
                std::memcpy(store.buffer.data(), &val, sizeof(double));
                SQLBindParameter(hstmt_, param_num, SQL_PARAM_INPUT, store.c_type, store.sql_type,
                                 store.col_size, store.dec_digits, store.buffer.data(), sizeof(double), &store.ind);
            } else if constexpr (std::is_same_v<T, std::string>) {
                store.c_type = SQL_C_CHAR;
                store.sql_type = SQL_VARCHAR;
                store.col_size = val.empty() ? 1 : val.size();
                store.dec_digits = 0;
                store.ind = static_cast<SQLLEN>(val.size());
                store.buffer.resize(val.size() + 1);
                std::memcpy(store.buffer.data(), val.c_str(), val.size() + 1);
                SQLBindParameter(hstmt_, param_num, SQL_PARAM_INPUT, store.c_type, store.sql_type,
                                 store.col_size, store.dec_digits, store.buffer.data(), store.buffer.size(), &store.ind);
            } else if constexpr (std::is_same_v<T, bool>) {
                store.c_type = SQL_C_BIT;
                store.sql_type = SQL_BIT;
                store.col_size = 1;
                store.dec_digits = 0;
                store.ind = 1;
                store.buffer.resize(1);
                store.buffer[0] = val ? 1 : 0;
                SQLBindParameter(hstmt_, param_num, SQL_PARAM_INPUT, store.c_type, store.sql_type,
                                 store.col_size, store.dec_digits, store.buffer.data(), 1, &store.ind);
            } else if constexpr (std::is_same_v<T, std::vector<uint8_t>>) {
                store.c_type = SQL_C_BINARY;
                store.sql_type = SQL_VARBINARY;
                store.col_size = val.empty() ? 1 : val.size();
                store.dec_digits = 0;
                store.ind = static_cast<SQLLEN>(val.size());
                store.buffer = val;
                SQLBindParameter(hstmt_, param_num, SQL_PARAM_INPUT, store.c_type, store.sql_type,
                                 store.col_size, store.dec_digits, store.buffer.data(), store.buffer.size(), &store.ind);
            }
        }, p);
    }
}

std::unique_ptr<IDataReader> MssqlPreparedStatement::execute_query() {
    apply_bindings();
    SQLRETURN rc = SQLExecute(hstmt_);
    check_rc(rc, SQL_HANDLE_STMT, hstmt_, "SQLExecute");
    return std::make_unique<MssqlDataReader>(hstmt_, false);
}

size_t MssqlPreparedStatement::execute_non_query() {
    apply_bindings();
    SQLRETURN rc = SQLExecute(hstmt_);
    check_rc(rc, SQL_HANDLE_STMT, hstmt_, "SQLExecute");
    SQLLEN row_count = 0;
    SQLRowCount(hstmt_, &row_count);
    return row_count > 0 ? static_cast<size_t>(row_count) : 0;
}

// ----------------------------------------------------------------------------
// MssqlConnection
// ----------------------------------------------------------------------------

MssqlConnection::MssqlConnection(std::string connection_string)
    : connection_string_(std::move(connection_string))
{}

MssqlConnection::~MssqlConnection() {
    close();
}

void MssqlConnection::open() {
    if (is_open_) return;

    SQLRETURN rc = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv_);
    check_rc(rc, SQL_HANDLE_ENV, henv_, "SQLAllocHandle(SQL_HANDLE_ENV)");

    rc = SQLSetEnvAttr(henv_, SQL_ATTR_ODBC_VERSION, reinterpret_cast<SQLPOINTER>(SQL_OV_ODBC3), 0);
    check_rc(rc, SQL_HANDLE_ENV, henv_, "SQLSetEnvAttr(SQL_ATTR_ODBC_VERSION)");

    rc = SQLAllocHandle(SQL_HANDLE_DBC, henv_, &hdbc_);
    check_rc(rc, SQL_HANDLE_ENV, henv_, "SQLAllocHandle(SQL_HANDLE_DBC)");

    std::vector<std::string> candidates;
    std::string trimmed = connection_string_;

    if (trimmed == "MSSQLLocalDB" || trimmed == "(localdb)\\MSSQLLocalDB" || trimmed == "localdb") {
        candidates.push_back("Driver={ODBC Driver 18 for SQL Server};Server=(localdb)\\MSSQLLocalDB;Database=tempdb;Trusted_Connection=yes;TrustServerCertificate=yes;");
        candidates.push_back("Driver={ODBC Driver 17 for SQL Server};Server=(localdb)\\MSSQLLocalDB;Database=tempdb;Trusted_Connection=yes;");
        candidates.push_back("Driver={SQL Server};Server=(localdb)\\MSSQLLocalDB;Database=tempdb;Trusted_Connection=yes;");
        candidates.push_back("DSN=MSSQLLocalDB;");
    } else if (trimmed.find("Driver=") == std::string::npos && trimmed.find("DRIVER=") == std::string::npos &&
               trimmed.rfind("DSN=", 0) != 0 && trimmed.rfind("dsn=", 0) != 0) {
        if (trimmed.find('=') == std::string::npos) {
            candidates.push_back("DSN=" + trimmed);
            candidates.push_back("Driver={ODBC Driver 18 for SQL Server};Server=" + trimmed + ";Database=tempdb;Trusted_Connection=yes;TrustServerCertificate=yes;");
            candidates.push_back("Driver={SQL Server};Server=" + trimmed + ";Database=tempdb;Trusted_Connection=yes;");
        } else {
            candidates.push_back("Driver={ODBC Driver 18 for SQL Server};TrustServerCertificate=yes;" + trimmed);
            candidates.push_back("Driver={ODBC Driver 17 for SQL Server};" + trimmed);
            candidates.push_back("Driver={SQL Server};" + trimmed);
            candidates.push_back(trimmed);
        }
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
        throw DbException("Failed to connect to Microsoft SQL Server (" + connection_string_ + "): " + last_err);
    }

    is_open_ = true;
}

void MssqlConnection::close() {
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

bool MssqlConnection::is_open() const {
    return is_open_;
}

std::unique_ptr<IPreparedStatement> MssqlConnection::prepare(std::string_view sql) {
    if (!is_open_) {
        throw DbException("Cannot prepare statement: MSSQL connection is not open");
    }
    return std::make_unique<MssqlPreparedStatement>(hdbc_, sql);
}

void MssqlConnection::execute(std::string_view sql) {
    if (!is_open_) {
        throw DbException("Cannot execute statement: MSSQL connection is not open");
    }
    SQLHSTMT hstmt = SQL_NULL_HSTMT;
    SQLRETURN rc = SQLAllocHandle(SQL_HANDLE_STMT, hdbc_, &hstmt);
    check_rc(rc, SQL_HANDLE_DBC, hdbc_, "SQLAllocHandle(SQL_HANDLE_STMT)");

    rc = SQLExecDirectA(hstmt, reinterpret_cast<SQLCHAR*>(const_cast<char*>(sql.data())), static_cast<SQLINTEGER>(sql.size()));
    if (!SQL_SUCCEEDED(rc) && rc != SQL_NO_DATA) {
        std::string err = get_odbc_error(SQL_HANDLE_STMT, hstmt);
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        throw DbException("MSSQL execute failed: " + err);
    }
    SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
}

void MssqlConnection::begin_transaction() {
    if (!is_open_) throw DbException("MSSQL connection is not open");
    SQLRETURN rc = SQLSetConnectAttr(hdbc_, SQL_ATTR_AUTOCOMMIT, reinterpret_cast<SQLPOINTER>(SQL_AUTOCOMMIT_OFF), SQL_IS_UINTEGER);
    check_rc(rc, SQL_HANDLE_DBC, hdbc_, "SQLSetConnectAttr(SQL_AUTOCOMMIT_OFF)");
}

void MssqlConnection::commit() {
    if (!is_open_) throw DbException("MSSQL connection is not open");
    SQLRETURN rc = SQLEndTran(SQL_HANDLE_DBC, hdbc_, SQL_COMMIT);
    check_rc(rc, SQL_HANDLE_DBC, hdbc_, "SQLEndTran(SQL_COMMIT)");
    SQLSetConnectAttr(hdbc_, SQL_ATTR_AUTOCOMMIT, reinterpret_cast<SQLPOINTER>(SQL_AUTOCOMMIT_ON), SQL_IS_UINTEGER);
}

void MssqlConnection::rollback() {
    if (!is_open_) throw DbException("MSSQL connection is not open");
    SQLRETURN rc = SQLEndTran(SQL_HANDLE_DBC, hdbc_, SQL_ROLLBACK);
    check_rc(rc, SQL_HANDLE_DBC, hdbc_, "SQLEndTran(SQL_ROLLBACK)");
    SQLSetConnectAttr(hdbc_, SQL_ATTR_AUTOCOMMIT, reinterpret_cast<SQLPOINTER>(SQL_AUTOCOMMIT_ON), SQL_IS_UINTEGER);
}

const ISqlDialect& MssqlConnection::dialect() const {
    return dialect_;
}

// ----------------------------------------------------------------------------
// make_connection<mssql> Specialization
// ----------------------------------------------------------------------------

template <>
std::unique_ptr<IConnection> make_connection<mssql>(const std::string& connection_string) {
    return std::make_unique<MssqlConnection>(connection_string);
}

} // namespace cpplinq

#endif // CPPLINQ_HAS_MSSQL
