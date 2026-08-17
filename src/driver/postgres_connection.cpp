#include "driver/postgres_connection.h"
#include "driver/odbc_utils.h"

#ifdef CPPLINQ_HAS_POSTGRES

#include <cstring>
#include <sstream>
#include <stdexcept>
#include <algorithm>

namespace cpplinq {

using detail::odbc::get_odbc_error;
using detail::odbc::check_rc;

// ----------------------------------------------------------------------------
// PgDataReader
// ----------------------------------------------------------------------------

PgDataReader::PgDataReader(SQLHSTMT hstmt, bool owns_stmt)
    : hstmt_(hstmt)
    , owns_stmt_(owns_stmt)
{
    if (hstmt_ != SQL_NULL_HSTMT) {
        SQLNumResultCols(hstmt_, &col_count_);
    }
}

PgDataReader::~PgDataReader() {
    if (owns_stmt_ && hstmt_ != SQL_NULL_HSTMT) {
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt_);
        hstmt_ = SQL_NULL_HSTMT;
    }
}

void PgDataReader::fetch_row_cache() {
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
                    col.str_val = val ? "true" : "false";
                }
                break;
            }
            case SQL_BINARY:
            case SQL_VARBINARY:
            case SQL_LONGVARBINARY: {
                SQLLEN ind = 0;
                uint8_t probe[1];
                SQLRETURN rc = SQLGetData(hstmt_, i, SQL_C_BINARY, probe, 0, &ind);
                if (SQL_SUCCEEDED(rc) && ind != SQL_NULL_DATA) {
                    col.is_null = false;
                    if (ind > 0) {
                        col.blob_val.resize(static_cast<size_t>(ind));
                        SQLGetData(hstmt_, i, SQL_C_BINARY, col.blob_val.data(), ind, &ind);
                    }
                }
                break;
            }
            default: { // Strings, dates, timestamps, fallback
                SQLLEN ind = 0;
                char buf[4096];
                SQLRETURN rc = SQLGetData(hstmt_, i, SQL_C_CHAR, buf, sizeof(buf), &ind);
                if (SQL_SUCCEEDED(rc) && ind != SQL_NULL_DATA) {
                    col.is_null = false;
                    if (ind >= 0 && ind < static_cast<SQLLEN>(sizeof(buf))) {
                        col.str_val.assign(buf, static_cast<size_t>(ind));
                    } else if (ind >= static_cast<SQLLEN>(sizeof(buf))) {
                        col.str_val.assign(buf, sizeof(buf) - 1);
                        size_t needed = static_cast<size_t>(ind) + 1;
                        std::vector<char> big_buf(needed);
                        std::memcpy(big_buf.data(), buf, sizeof(buf) - 1);
                        SQLLEN ind2 = 0;
                        SQLGetData(hstmt_, i, SQL_C_CHAR, big_buf.data() + sizeof(buf) - 1, static_cast<SQLLEN>(needed - sizeof(buf) + 1), &ind2);
                        col.str_val = big_buf.data();
                    } else {
                        col.str_val = buf;
                    }
                    try { col.int_val = std::stoll(col.str_val); } catch (...) {}
                    try { col.double_val = std::stod(col.str_val); } catch (...) {}
                    col.bool_val = (col.str_val == "1" || col.str_val == "true" || col.str_val == "t" || col.str_val == "TRUE" || col.str_val == "T");
                }
                break;
            }
        }
    }
}

bool PgDataReader::next() {
    if (hstmt_ == SQL_NULL_HSTMT) return false;
    SQLRETURN rc = SQLFetch(hstmt_);
    if (rc == SQL_NO_DATA) return false;
    if (!SQL_SUCCEEDED(rc)) {
        check_rc(rc, SQL_HANDLE_STMT, hstmt_, "SQLFetch");
    }
    fetch_row_cache();
    return true;
}

int PgDataReader::column_count() const {
    return col_count_;
}

bool PgDataReader::is_null(int col) const {
    if (col < 0 || col >= static_cast<int>(row_cache_.size())) return true;
    return row_cache_[col].is_null;
}

int64_t PgDataReader::get_int64(int col) const {
    if (is_null(col)) return 0;
    return row_cache_[col].int_val;
}

double PgDataReader::get_double(int col) const {
    if (is_null(col)) return 0.0;
    return row_cache_[col].double_val;
}

std::string PgDataReader::get_string(int col) const {
    if (is_null(col)) return {};
    return row_cache_[col].str_val;
}

bool PgDataReader::get_bool(int col) const {
    if (is_null(col)) return false;
    return row_cache_[col].bool_val;
}

std::vector<uint8_t> PgDataReader::get_blob(int col) const {
    if (is_null(col)) return {};
    return row_cache_[col].blob_val;
}

// ----------------------------------------------------------------------------
// PgPreparedStatement
// ----------------------------------------------------------------------------

PgPreparedStatement::PgPreparedStatement(SQLHDBC hdbc, std::string_view sql)
    : hdbc_(hdbc)
    , sql_(sql)
{
    SQLRETURN rc = SQLAllocHandle(SQL_HANDLE_STMT, hdbc_, &hstmt_);
    check_rc(rc, SQL_HANDLE_DBC, hdbc_, "SQLAllocHandle(SQL_HANDLE_STMT)");

    rc = SQLPrepareA(
        hstmt_,
        reinterpret_cast<SQLCHAR*>(const_cast<char*>(sql_.data())),
        static_cast<SQLINTEGER>(sql_.size())
    );
    check_rc(rc, SQL_HANDLE_STMT, hstmt_, "SQLPrepareA");
}

PgPreparedStatement::~PgPreparedStatement() {
    if (hstmt_ != SQL_NULL_HSTMT) {
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt_);
        hstmt_ = SQL_NULL_HSTMT;
    }
}

void PgPreparedStatement::bind(int index, const BoundValue& value) {
    if (index < 0) {
        throw DbException("Invalid parameter index: " + std::to_string(index));
    }
    if (static_cast<size_t>(index) >= params_.size()) {
        params_.resize(index + 1);
    }
    params_[index] = value;
}

void PgPreparedStatement::apply_bindings() {
    storage_.resize(params_.size());

    for (size_t i = 0; i < params_.size(); ++i) {
        auto& p = params_[i];
        auto& s = storage_[i];

        std::visit([&](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                s.ind = SQL_NULL_DATA;
                s.c_type = SQL_C_CHAR;
                s.sql_type = SQL_VARCHAR;
                s.col_size = 1;
                s.dec_digits = 0;
            } else if constexpr (std::is_same_v<T, int64_t>) {
                s.buffer.resize(sizeof(int64_t));
                std::memcpy(s.buffer.data(), &v, sizeof(int64_t));
                s.ind = sizeof(int64_t);
                s.c_type = SQL_C_SBIGINT;
                s.sql_type = SQL_BIGINT;
                s.col_size = 19;
                s.dec_digits = 0;
            } else if constexpr (std::is_same_v<T, double>) {
                s.buffer.resize(sizeof(double));
                std::memcpy(s.buffer.data(), &v, sizeof(double));
                s.ind = sizeof(double);
                s.c_type = SQL_C_DOUBLE;
                s.sql_type = SQL_DOUBLE;
                s.col_size = 53;
                s.dec_digits = 0;
            } else if constexpr (std::is_same_v<T, bool>) {
                unsigned char b = v ? 1 : 0;
                s.buffer.resize(1);
                s.buffer[0] = b;
                s.ind = 1;
                s.c_type = SQL_C_BIT;
                s.sql_type = SQL_BIT;
                s.col_size = 1;
                s.dec_digits = 0;
            } else if constexpr (std::is_same_v<T, std::string>) {
                s.buffer.assign(v.begin(), v.end());
                s.buffer.push_back('\0');
                s.ind = static_cast<SQLLEN>(v.size());
                s.c_type = SQL_C_CHAR;
                s.sql_type = SQL_VARCHAR;
                s.col_size = std::max<SQLULEN>(1, static_cast<SQLULEN>(v.size()));
                s.dec_digits = 0;
            } else if constexpr (std::is_same_v<T, std::vector<uint8_t>>) {
                s.buffer = v;
                s.ind = static_cast<SQLLEN>(v.size());
                s.c_type = SQL_C_BINARY;
                s.sql_type = SQL_VARBINARY;
                s.col_size = std::max<SQLULEN>(1, static_cast<SQLULEN>(v.size()));
                s.dec_digits = 0;
            }
        }, p);

        SQLRETURN rc = SQLBindParameter(
            hstmt_,
            static_cast<SQLUSMALLINT>(i + 1),
            SQL_PARAM_INPUT,
            s.c_type,
            s.sql_type,
            s.col_size,
            s.dec_digits,
            s.ind == SQL_NULL_DATA ? nullptr : s.buffer.data(),
            static_cast<SQLLEN>(s.buffer.size()),
            &s.ind
        );
        check_rc(rc, SQL_HANDLE_STMT, hstmt_, "SQLBindParameter");
    }
}

std::unique_ptr<IDataReader> PgPreparedStatement::execute_query() {
    apply_bindings();
    SQLRETURN rc = SQLExecute(hstmt_);
    if (!SQL_SUCCEEDED(rc) && rc != SQL_NO_DATA) {
        check_rc(rc, SQL_HANDLE_STMT, hstmt_, "SQLExecute(execute_query)");
    }
    return std::make_unique<PgDataReader>(hstmt_, false);
}

size_t PgPreparedStatement::execute_non_query() {
    apply_bindings();
    SQLRETURN rc = SQLExecute(hstmt_);
    if (!SQL_SUCCEEDED(rc) && rc != SQL_NO_DATA) {
        check_rc(rc, SQL_HANDLE_STMT, hstmt_, "SQLExecute(execute_non_query)");
    }
    SQLLEN affected = 0;
    SQLRowCount(hstmt_, &affected);
    return affected > 0 ? static_cast<size_t>(affected) : 0;
}

void PgPreparedStatement::reset() {
    if (hstmt_ != SQL_NULL_HSTMT) {
        SQLFreeStmt(hstmt_, SQL_RESET_PARAMS);
        SQLFreeStmt(hstmt_, SQL_UNBIND);
    }
    params_.clear();
    storage_.clear();
}

void PgPreparedStatement::cancel() {
    if (hstmt_ != SQL_NULL_HSTMT) {
        SQLCancel(hstmt_);
    }
}

void PgPreparedStatement::set_timeout(uint32_t seconds) {
    if (hstmt_ != SQL_NULL_HSTMT) {
        SQLSetStmtAttr(
            hstmt_,
            SQL_ATTR_QUERY_TIMEOUT,
            reinterpret_cast<SQLPOINTER>(static_cast<uintptr_t>(seconds)),
            SQL_IS_UINTEGER
        );
    }
}

void PgPreparedStatement::set_stop_token(std::stop_token token) {
    stop_token_ = token;
    if (token.stop_possible()) {
        stop_cb_.emplace(token, [this]() {
            cancel();
        });
    }
}

// ----------------------------------------------------------------------------
// PgConnection
// ----------------------------------------------------------------------------

PgConnection::PgConnection(std::string connection_string)
    : connection_string_(std::move(connection_string))
{}

PgConnection::~PgConnection() {
    try {
        close();
    } catch (...) {}
}

void PgConnection::open() {
    if (is_open_) return;

    SQLRETURN rc = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv_);
    check_rc(rc, SQL_HANDLE_ENV, henv_, "SQLAllocHandle(SQL_HANDLE_ENV)");

    rc = SQLSetEnvAttr(henv_, SQL_ATTR_ODBC_VERSION, reinterpret_cast<SQLPOINTER>(SQL_OV_ODBC3), 0);
    check_rc(rc, SQL_HANDLE_ENV, henv_, "SQLSetEnvAttr(SQL_OV_ODBC3)");

    rc = SQLAllocHandle(SQL_HANDLE_DBC, henv_, &hdbc_);
    check_rc(rc, SQL_HANDLE_ENV, henv_, "SQLAllocHandle(SQL_HANDLE_DBC)");

    std::string trimmed = connection_string_;
    while (!trimmed.empty() && (trimmed.front() == ' ' || trimmed.front() == '\t')) trimmed.erase(trimmed.begin());
    while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\t')) trimmed.pop_back();

    std::vector<std::string> candidates;
    if (trimmed.find('=') == std::string::npos && trimmed.rfind("postgres://", 0) != 0 && trimmed.rfind("postgresql://", 0) != 0) {
        // Plain DSN name
        candidates.push_back("DSN=" + trimmed + ";");
        candidates.push_back(trimmed);
    } else if (trimmed.find("Driver=") != std::string::npos || trimmed.find("driver=") != std::string::npos ||
               trimmed.find("DSN=") != std::string::npos || trimmed.find("dsn=") != std::string::npos) {
        candidates.push_back(trimmed);
    } else {
        // Build driver candidate list
        candidates.push_back("Driver={PostgreSQL Unicode};" + trimmed);
        candidates.push_back("Driver={PostgreSQL ANSI};" + trimmed);
        candidates.push_back("Driver={PostgreSQL Unicode(x64)};" + trimmed);
        candidates.push_back("Driver={PostgreSQL ANSI(x64)};" + trimmed);
        candidates.push_back("Driver={PostgreSQL};" + trimmed);
        candidates.push_back(trimmed);
        candidates.push_back("DSN=" + trimmed + ";");
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
        throw DbException("Failed to connect to PostgreSQL (" + connection_string_ + "): " + last_err);
    }

    is_open_ = true;
}

void PgConnection::close() {
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

bool PgConnection::is_open() const {
    return is_open_;
}

std::unique_ptr<IPreparedStatement> PgConnection::prepare(std::string_view sql) {
    if (!is_open_) {
        throw DbException("Cannot prepare statement: PostgreSQL connection is not open");
    }
    return std::make_unique<PgPreparedStatement>(hdbc_, sql);
}

void PgConnection::execute(std::string_view sql) {
    if (!is_open_) {
        throw DbException("Cannot execute statement: PostgreSQL connection is not open");
    }
    SQLHSTMT hstmt = SQL_NULL_HSTMT;
    SQLRETURN rc = SQLAllocHandle(SQL_HANDLE_STMT, hdbc_, &hstmt);
    check_rc(rc, SQL_HANDLE_DBC, hdbc_, "SQLAllocHandle(SQL_HANDLE_STMT)");

    rc = SQLExecDirectA(hstmt, reinterpret_cast<SQLCHAR*>(const_cast<char*>(sql.data())), static_cast<SQLINTEGER>(sql.size()));
    if (!SQL_SUCCEEDED(rc) && rc != SQL_NO_DATA) {
        std::string err = get_odbc_error(SQL_HANDLE_STMT, hstmt);
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        throw DbException("PostgreSQL execute failed: " + err);
    }
    SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
}

void PgConnection::begin_transaction() {
    if (!is_open_) throw DbException("PostgreSQL connection is not open");
    SQLRETURN rc = SQLSetConnectAttr(hdbc_, SQL_ATTR_AUTOCOMMIT, reinterpret_cast<SQLPOINTER>(SQL_AUTOCOMMIT_OFF), SQL_IS_UINTEGER);
    check_rc(rc, SQL_HANDLE_DBC, hdbc_, "SQLSetConnectAttr(SQL_AUTOCOMMIT_OFF)");
}

void PgConnection::commit() {
    if (!is_open_) throw DbException("PostgreSQL connection is not open");
    SQLRETURN rc = SQLEndTran(SQL_HANDLE_DBC, hdbc_, SQL_COMMIT);
    check_rc(rc, SQL_HANDLE_DBC, hdbc_, "SQLEndTran(SQL_COMMIT)");
    SQLSetConnectAttr(hdbc_, SQL_ATTR_AUTOCOMMIT, reinterpret_cast<SQLPOINTER>(SQL_AUTOCOMMIT_ON), SQL_IS_UINTEGER);
}

void PgConnection::rollback() {
    if (!is_open_) throw DbException("PostgreSQL connection is not open");
    SQLRETURN rc = SQLEndTran(SQL_HANDLE_DBC, hdbc_, SQL_ROLLBACK);
    check_rc(rc, SQL_HANDLE_DBC, hdbc_, "SQLEndTran(SQL_ROLLBACK)");
    SQLSetConnectAttr(hdbc_, SQL_ATTR_AUTOCOMMIT, reinterpret_cast<SQLPOINTER>(SQL_AUTOCOMMIT_ON), SQL_IS_UINTEGER);
}

const ISqlDialect& PgConnection::dialect() const {
    return dialect_;
}

DriverInfo PgConnection::info() const {
    DriverInfo i;
    i.driver_name = "PostgreSQL ODBC Driver";
    i.dbms_name = "PostgreSQL";
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

DriverCapabilities PgConnection::capabilities() const {
    DriverCapabilities caps;
    caps.cancel = true;
    caps.streaming = true;
    caps.query_timeout = true;
    caps.transactions = true;
    caps.savepoints = true;
    caps.returning_clause = true;
    caps.output_clause = false;
    caps.upsert = true;
    caps.array_batch_insert = true;
    caps.default_batch_chunk_size = 1000;
    caps.window_functions = true;
    caps.ctes = true;
    return caps;
}

size_t PgConnection::insert_many_batch(
    std::string_view sql,
    const std::vector<BoundValue>& flat_params,
    size_t col_count,
    size_t row_count
) {
    return detail::odbc::execute_insert_many_batch(
        hdbc_, sql, flat_params, col_count, row_count, "PgConnection"
    );
}

// ----------------------------------------------------------------------------
// make_connection<postgres> Specialization
// ----------------------------------------------------------------------------

template <>
std::unique_ptr<IConnection> make_connection<postgres>(const std::string& connection_string) {
    return std::make_unique<PgConnection>(connection_string);
}

} // namespace cpplinq

#endif // CPPLINQ_HAS_POSTGRES
