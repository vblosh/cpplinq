#include "driver/odbc_connection.h"
#include "driver/odbc_utils.h"

#if defined(CPPLINQ_HAS_MSSQL) || defined(CPPLINQ_HAS_MYSQL) || defined(CPPLINQ_HAS_POSTGRES) || defined(CPPLINQ_HAS_INFORMIX) || defined(CPPLINQ_HAS_ORACLE)

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
        init_bound_columns();
    }
}

OdbcDataReader::~OdbcDataReader() {
    if (hstmt_ != SQL_NULL_HSTMT) {
        SQLFreeStmt(hstmt_, SQL_UNBIND);
        if (owns_stmt_) {
            SQLFreeHandle(SQL_HANDLE_STMT, hstmt_);
        }
        hstmt_ = SQL_NULL_HSTMT;
    }
}

void OdbcDataReader::init_bound_columns() {
    bound_cols_.resize(col_count_);
    for (SQLSMALLINT i = 1; i <= col_count_; ++i) {
        auto& col = bound_cols_[i - 1];
        SQLCHAR col_name[256];
        SQLSMALLINT name_len = 0;
        SQLDescribeColA(hstmt_, i, col_name, sizeof(col_name), &name_len,
                        &col.data_type,
                        &col.col_size,
                        &col.dec_digits,
                        &col.nullable);

        switch (col.data_type) {
            case SQL_BIGINT: {
                col.c_type = SQL_C_UBIGINT;
                SQLBindCol(hstmt_, i, col.c_type, &col.uint_val, sizeof(col.uint_val), &col.ind);
                break;
            }
            case SQL_INTEGER:
            case SQL_SMALLINT:
            case SQL_TINYINT: {
                col.c_type = SQL_C_SBIGINT;
                SQLBindCol(hstmt_, i, col.c_type, &col.int_val, sizeof(col.int_val), &col.ind);
                break;
            }
            case SQL_FLOAT:
            case SQL_REAL:
            case SQL_DOUBLE: {
                col.c_type = SQL_C_DOUBLE;
                SQLBindCol(hstmt_, i, col.c_type, &col.double_val, sizeof(col.double_val), &col.ind);
                break;
            }
            case SQL_BIT: {
                col.c_type = SQL_C_BIT;
                SQLBindCol(hstmt_, i, col.c_type, &col.bool_val, sizeof(col.bool_val), &col.ind);
                break;
            }
            case SQL_TYPE_DATE:
#ifdef SQL_DATE
            case SQL_DATE:
#endif
            {
                col.c_type = SQL_C_TYPE_DATE;
                SQLBindCol(hstmt_, i, col.c_type, &col.date_val, sizeof(col.date_val), &col.ind);
                break;
            }
            case SQL_TYPE_TIME:
#ifdef SQL_TIME
            case SQL_TIME:
#endif
#ifdef SQL_SS_TIME2
            case SQL_SS_TIME2:
#endif
            {
                col.c_type = SQL_C_TYPE_TIME;
                SQLBindCol(hstmt_, i, col.c_type, &col.time_val, sizeof(col.time_val), &col.ind);
                break;
            }
            case SQL_TYPE_TIMESTAMP:
#ifdef SQL_TIMESTAMP
            case SQL_TIMESTAMP:
#endif
#ifdef SQL_SS_TIMESTAMPOFFSET
            case SQL_SS_TIMESTAMPOFFSET:
#endif
            {
                col.c_type = SQL_C_TYPE_TIMESTAMP;
                SQLBindCol(hstmt_, i, col.c_type, &col.timestamp_val, sizeof(col.timestamp_val), &col.ind);
                break;
            }
            case SQL_GUID: {
                col.c_type = SQL_C_GUID;
                SQLBindCol(hstmt_, i, col.c_type, &col.guid_val, sizeof(col.guid_val), &col.ind);
                break;
            }
            case SQL_INTERVAL_YEAR:
            case SQL_INTERVAL_MONTH:
            case SQL_INTERVAL_DAY:
            case SQL_INTERVAL_HOUR:
            case SQL_INTERVAL_MINUTE:
            case SQL_INTERVAL_SECOND:
            case SQL_INTERVAL_YEAR_TO_MONTH:
            case SQL_INTERVAL_DAY_TO_HOUR:
            case SQL_INTERVAL_DAY_TO_MINUTE:
            case SQL_INTERVAL_DAY_TO_SECOND:
            case SQL_INTERVAL_HOUR_TO_MINUTE:
            case SQL_INTERVAL_HOUR_TO_SECOND:
            case SQL_INTERVAL_MINUTE_TO_SECOND: {
                col.c_type = SQL_C_INTERVAL_DAY_TO_SECOND;
                SQLBindCol(hstmt_, i, col.c_type, &col.interval_val, sizeof(col.interval_val), &col.ind);
                break;
            }
            case SQL_DECIMAL:
            case SQL_NUMERIC: {
                col.c_type = SQL_C_CHAR;
                col.buffer.resize(64, 0);
                SQLBindCol(hstmt_, i, col.c_type, col.buffer.data(), static_cast<SQLLEN>(col.buffer.size()), &col.ind);
                break;
            }
            case SQL_WCHAR:
            case SQL_WVARCHAR:
            case SQL_WLONGVARCHAR: {
                col.c_type = SQL_C_WCHAR;
                size_t buf_chars = (col.col_size > 0 && col.col_size <= 2048) ? (col.col_size + 1) : 2048;
                col.buffer.resize(buf_chars * sizeof(SQLWCHAR), 0);
                SQLBindCol(hstmt_, i, col.c_type, col.buffer.data(), static_cast<SQLLEN>(col.buffer.size()), &col.ind);
                break;
            }
            case SQL_BINARY:
            case SQL_VARBINARY:
            case SQL_LONGVARBINARY: {
                col.c_type = SQL_C_BINARY;
                size_t buf_bytes = (col.col_size > 0 && col.col_size <= 4096) ? col.col_size : 4096;
                col.buffer.resize(buf_bytes, 0);
                SQLBindCol(hstmt_, i, col.c_type, col.buffer.data(), static_cast<SQLLEN>(col.buffer.size()), &col.ind);
                break;
            }
            case SQL_CHAR:
            case SQL_VARCHAR:
            case SQL_LONGVARCHAR:
            default: {
                col.c_type = SQL_C_CHAR;
                size_t buf_bytes = (col.col_size > 0 && col.col_size <= 4096) ? (col.col_size + 1) : 4096;
                col.buffer.resize(buf_bytes, 0);
                SQLBindCol(hstmt_, i, col.c_type, col.buffer.data(), static_cast<SQLLEN>(col.buffer.size()), &col.ind);
                break;
            }
        }
    }
}

bool OdbcDataReader::next() {
    if (hstmt_ == SQL_NULL_HSTMT) return false;
    for (auto& col : bound_cols_) {
        col.str_cache.clear();
        col.wstr_cache.clear();
    }
    SQLRETURN rc = SQLFetch(hstmt_);
    if (rc == SQL_NO_DATA) {
        return false;
    }
    if (!SQL_SUCCEEDED(rc)) {
        check_rc(rc, SQL_HANDLE_STMT, hstmt_, "SQLFetch");
        return false;
    }
    return true;
}

int OdbcDataReader::column_count() const {
    return static_cast<int>(col_count_);
}

bool OdbcDataReader::is_null(int col) const {
    if (col < 0 || col >= static_cast<int>(bound_cols_.size())) return true;
    return bound_cols_[col].ind == SQL_NULL_DATA;
}

int64_t OdbcDataReader::get_int64(int col) const {
    if (is_null(col)) return 0;
    const auto& c = bound_cols_[col];
    switch (c.c_type) {
        case SQL_C_SBIGINT: return c.int_val;
        case SQL_C_UBIGINT: return static_cast<int64_t>(c.uint_val);
        case SQL_C_DOUBLE: return static_cast<int64_t>(c.double_val);
        case SQL_C_BIT: return c.bool_val ? 1 : 0;
        case SQL_C_CHAR: {
            std::string_view sv = get_string_view(col);
            try { return std::stoll(std::string(sv)); } catch (...) { return 0; }
        }
        case SQL_C_WCHAR: {
            std::wstring ws = get_wstring(col);
            try { return std::stoll(wstring_to_utf8(ws)); } catch (...) { return 0; }
        }
        default: return 0;
    }
}

uint64_t OdbcDataReader::get_uint64(int col) const {
    if (is_null(col)) return 0;
    const auto& c = bound_cols_[col];
    switch (c.c_type) {
        case SQL_C_UBIGINT: return c.uint_val;
        case SQL_C_SBIGINT: return static_cast<uint64_t>(c.int_val);
        case SQL_C_DOUBLE: return static_cast<uint64_t>(c.double_val > 0 ? c.double_val : 0);
        case SQL_C_BIT: return c.bool_val ? 1 : 0;
        case SQL_C_CHAR: {
            std::string_view sv = get_string_view(col);
            try { return std::stoull(std::string(sv)); } catch (...) { return 0; }
        }
        case SQL_C_WCHAR: {
            std::wstring ws = get_wstring(col);
            try { return std::stoull(wstring_to_utf8(ws)); } catch (...) { return 0; }
        }
        default: return 0;
    }
}

double OdbcDataReader::get_double(int col) const {
    if (is_null(col)) return 0.0;
    const auto& c = bound_cols_[col];
    switch (c.c_type) {
        case SQL_C_DOUBLE: return c.double_val;
        case SQL_C_SBIGINT: return static_cast<double>(c.int_val);
        case SQL_C_UBIGINT: return static_cast<double>(c.uint_val);
        case SQL_C_BIT: return c.bool_val ? 1.0 : 0.0;
        case SQL_C_CHAR: {
            std::string_view sv = get_string_view(col);
            try { return std::stod(std::string(sv)); } catch (...) { return 0.0; }
        }
        case SQL_C_WCHAR: {
            std::wstring ws = get_wstring(col);
            try { return std::stod(wstring_to_utf8(ws)); } catch (...) { return 0.0; }
        }
        default: return 0.0;
    }
}

void OdbcDataReader::ensure_str(const BoundCol& c) const {
    if (!c.str_cache.empty() || c.ind == SQL_NULL_DATA) return;
    switch (c.c_type) {
        case SQL_C_CHAR: {
            if (c.buffer.empty()) return;
            size_t len = 0;
            if (c.ind >= 0) {
                len = std::min(static_cast<size_t>(c.ind), c.buffer.size() > 0 ? c.buffer.size() - 1 : 0);
            } else {
                len = ::strnlen(reinterpret_cast<const char*>(c.buffer.data()), c.buffer.size());
            }
            c.str_cache.assign(reinterpret_cast<const char*>(c.buffer.data()), len);
            break;
        }
        case SQL_C_SBIGINT:
            c.str_cache = std::to_string(c.int_val);
            break;
        case SQL_C_UBIGINT:
            c.str_cache = std::to_string(c.uint_val);
            break;
        case SQL_C_DOUBLE:
            c.str_cache = std::to_string(c.double_val);
            break;
        case SQL_C_BIT:
            c.str_cache = c.bool_val ? "1" : "0";
            break;
        case SQL_C_TYPE_DATE:
            c.str_cache = SqlDate(c.date_val.year, static_cast<uint8_t>(c.date_val.month), static_cast<uint8_t>(c.date_val.day)).to_string();
            break;
        case SQL_C_TYPE_TIME:
            c.str_cache = SqlTime(static_cast<uint8_t>(c.time_val.hour), static_cast<uint8_t>(c.time_val.minute), static_cast<uint8_t>(c.time_val.second)).to_string();
            break;
        case SQL_C_TYPE_TIMESTAMP:
            c.str_cache = SqlTimestamp(c.timestamp_val.year, static_cast<uint8_t>(c.timestamp_val.month), static_cast<uint8_t>(c.timestamp_val.day),
                                       static_cast<uint8_t>(c.timestamp_val.hour), static_cast<uint8_t>(c.timestamp_val.minute), static_cast<uint8_t>(c.timestamp_val.second),
                                       c.timestamp_val.fraction).to_string(c.timestamp_val.fraction > 0);
            break;
        case SQL_C_INTERVAL_DAY_TO_SECOND:
            c.str_cache = detail::odbc::odbc_struct_to_interval(c.interval_val).to_string();
            break;
        case SQL_C_GUID:
            c.str_cache = detail::odbc::odbc_struct_to_guid(c.guid_val).to_string();
            break;
        case SQL_C_WCHAR: {
            size_t len_bytes = (c.ind >= 0) ? std::min(static_cast<size_t>(c.ind), c.buffer.size()) : c.buffer.size();
            size_t len_wchars = len_bytes / sizeof(SQLWCHAR);
            const auto* wptr = reinterpret_cast<const SQLWCHAR*>(c.buffer.data());
            std::wstring ws = detail::odbc::sqlwchar_to_wstring(wptr, len_wchars);
            c.str_cache = wstring_to_utf8(ws);
            break;
        }
        default:
            break;
    }
}

std::string_view OdbcDataReader::get_string_view(int col) const {
    if (is_null(col)) return {};
    const auto& c = bound_cols_[col];
    if (c.c_type == SQL_C_CHAR) {
        if (!c.str_cache.empty()) return c.str_cache;
        if (c.buffer.empty()) return {};
        size_t len = 0;
        if (c.ind >= 0) {
            len = std::min(static_cast<size_t>(c.ind), c.buffer.size() > 0 ? c.buffer.size() - 1 : 0);
        } else {
            len = ::strnlen(reinterpret_cast<const char*>(c.buffer.data()), c.buffer.size());
        }
        return std::string_view(reinterpret_cast<const char*>(c.buffer.data()), len);
    }
    ensure_str(c);
    return c.str_cache;
}

std::string OdbcDataReader::get_string(int col) const {
    return std::string(get_string_view(col));
}

std::wstring OdbcDataReader::get_wstring(int col) const {
    if (is_null(col)) return {};
    const auto& c = bound_cols_[col];
    if (c.c_type == SQL_C_WCHAR) {
        if (!c.wstr_cache.empty()) return c.wstr_cache;
        size_t len_bytes = (c.ind >= 0) ? std::min(static_cast<size_t>(c.ind), c.buffer.size()) : c.buffer.size();
        size_t len_wchars = len_bytes / sizeof(SQLWCHAR);
        const auto* wptr = reinterpret_cast<const SQLWCHAR*>(c.buffer.data());
        c.wstr_cache = detail::odbc::sqlwchar_to_wstring(wptr, len_wchars);
        return c.wstr_cache;
    }
    return utf8_to_wstring(get_string(col));
}

bool OdbcDataReader::get_bool(int col) const {
    if (is_null(col)) return false;
    const auto& c = bound_cols_[col];
    switch (c.c_type) {
        case SQL_C_BIT: return c.bool_val != 0;
        case SQL_C_SBIGINT: return c.int_val != 0;
        case SQL_C_DOUBLE: return c.double_val != 0.0;
        case SQL_C_CHAR: {
            std::string_view sv = get_string_view(col);
            return (sv == "1" || sv == "true" || sv == "TRUE" || sv == "t" || sv == "T");
        }
        case SQL_C_WCHAR: {
            std::wstring ws = get_wstring(col);
            return (ws == L"1" || ws == L"true" || ws == L"TRUE" || ws == L"t" || ws == L"T");
        }
        default: return false;
    }
}

std::vector<uint8_t> OdbcDataReader::get_blob(int col) const {
    if (is_null(col)) return {};
    const auto& c = bound_cols_[col];
    if (c.c_type == SQL_C_BINARY) {
        size_t len = (c.ind >= 0) ? std::min(static_cast<size_t>(c.ind), c.buffer.size()) : 0;
        return std::vector<uint8_t>(c.buffer.data(), c.buffer.data() + len);
    }
    return {};
}

SqlNumeric OdbcDataReader::get_numeric(int col) const {
    if (is_null(col)) return SqlNumeric("0");
    const auto& c = bound_cols_[col];
    if (c.data_type == SQL_DECIMAL || c.data_type == SQL_NUMERIC || c.c_type == SQL_C_CHAR) {
        return SqlNumeric(get_string(col));
    }
    if (c.c_type == SQL_C_SBIGINT) return SqlNumeric(c.int_val);
    if (c.c_type == SQL_C_DOUBLE) return SqlNumeric(c.double_val);
    return SqlNumeric("0");
}

SqlDate OdbcDataReader::get_date(int col) const {
    if (is_null(col)) return SqlDate();
    const auto& c = bound_cols_[col];
    if (c.c_type == SQL_C_TYPE_DATE) {
        return SqlDate(c.date_val.year, static_cast<uint8_t>(c.date_val.month), static_cast<uint8_t>(c.date_val.day));
    }
    if (c.c_type == SQL_C_TYPE_TIMESTAMP) {
        return SqlDate(c.timestamp_val.year, static_cast<uint8_t>(c.timestamp_val.month), static_cast<uint8_t>(c.timestamp_val.day));
    }
    return SqlDate::from_string(get_string(col));
}

SqlTime OdbcDataReader::get_time(int col) const {
    if (is_null(col)) return SqlTime();
    const auto& c = bound_cols_[col];
    if (c.c_type == SQL_C_TYPE_TIME) {
        return SqlTime(static_cast<uint8_t>(c.time_val.hour), static_cast<uint8_t>(c.time_val.minute), static_cast<uint8_t>(c.time_val.second));
    }
    if (c.c_type == SQL_C_TYPE_TIMESTAMP) {
        return SqlTime(static_cast<uint8_t>(c.timestamp_val.hour), static_cast<uint8_t>(c.timestamp_val.minute), static_cast<uint8_t>(c.timestamp_val.second), c.timestamp_val.fraction);
    }
    return SqlTime::from_string(get_string(col));
}

SqlTimestamp OdbcDataReader::get_timestamp(int col) const {
    if (is_null(col)) return SqlTimestamp();
    const auto& c = bound_cols_[col];
    if (c.c_type == SQL_C_TYPE_TIMESTAMP) {
        return SqlTimestamp(c.timestamp_val.year, static_cast<uint8_t>(c.timestamp_val.month), static_cast<uint8_t>(c.timestamp_val.day),
                            static_cast<uint8_t>(c.timestamp_val.hour), static_cast<uint8_t>(c.timestamp_val.minute), static_cast<uint8_t>(c.timestamp_val.second),
                            c.timestamp_val.fraction);
    }
    if (c.c_type == SQL_C_TYPE_DATE) {
        return SqlTimestamp(c.date_val.year, static_cast<uint8_t>(c.date_val.month), static_cast<uint8_t>(c.date_val.day));
    }
    return SqlTimestamp::from_string(get_string(col));
}

SqlInterval OdbcDataReader::get_interval(int col) const {
    if (is_null(col)) return SqlInterval();
    const auto& c = bound_cols_[col];
    if (c.c_type == SQL_C_INTERVAL_DAY_TO_SECOND) {
        return detail::odbc::odbc_struct_to_interval(c.interval_val);
    }
    return SqlInterval::from_string(get_string(col));
}

SqlGuid OdbcDataReader::get_guid(int col) const {
    if (is_null(col)) return SqlGuid();
    const auto& c = bound_cols_[col];
    if (c.c_type == SQL_C_GUID) {
        return detail::odbc::odbc_struct_to_guid(c.guid_val);
    }
    return SqlGuid::from_string(get_string(col));
}

BoundValue OdbcDataReader::get_value(int col) const {
    if (is_null(col)) return std::monostate{};
    const auto& c = bound_cols_[col];
    switch (c.c_type) {
        case SQL_C_SBIGINT: return c.int_val;
        case SQL_C_UBIGINT: return c.uint_val;
        case SQL_C_DOUBLE: return c.double_val;
        case SQL_C_BIT: return c.bool_val != 0;
        case SQL_C_TYPE_DATE: return get_date(col);
        case SQL_C_TYPE_TIME: return get_time(col);
        case SQL_C_TYPE_TIMESTAMP: return get_timestamp(col);
        case SQL_C_INTERVAL_DAY_TO_SECOND: return get_interval(col);
        case SQL_C_GUID: return get_guid(col);
        case SQL_C_BINARY: return get_blob(col);
        case SQL_C_WCHAR: return get_wstring(col);
        case SQL_C_CHAR:
        default:
            if (c.data_type == SQL_DECIMAL || c.data_type == SQL_NUMERIC) {
                return SqlNumeric(get_string(col));
            }
            return std::string(get_string_view(col));
    }
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
                std::string s = std::to_string(val);
                store.c_type = SQL_C_CHAR;
                store.sql_type = SQL_NUMERIC;
                store.col_size = 19;
                store.dec_digits = 0;
                store.ind = static_cast<SQLLEN>(s.size());
                store.buffer.resize(s.size() + 1);
                std::memcpy(store.buffer.data(), s.c_str(), s.size() + 1);
                SQLRETURN rc = SQLBindParameter(hstmt_, param_num, SQL_PARAM_INPUT, store.c_type, store.sql_type,
                                                store.col_size, store.dec_digits, store.buffer.data(), static_cast<SQLLEN>(store.buffer.size()), &store.ind);
                check_rc(rc, SQL_HANDLE_STMT, hstmt_, "SQLBindParameter(int64)");
            } else if constexpr (std::is_same_v<T, uint64_t>) {
                std::string s = std::to_string(val);
                store.c_type = SQL_C_CHAR;
                store.sql_type = SQL_NUMERIC;
                store.col_size = 20;
                store.dec_digits = 0;
                store.ind = static_cast<SQLLEN>(s.size());
                store.buffer.resize(s.size() + 1);
                std::memcpy(store.buffer.data(), s.c_str(), s.size() + 1);
                SQLRETURN rc = SQLBindParameter(hstmt_, param_num, SQL_PARAM_INPUT, store.c_type, store.sql_type,
                                                store.col_size, store.dec_digits, store.buffer.data(), static_cast<SQLLEN>(store.buffer.size()), &store.ind);
                check_rc(rc, SQL_HANDLE_STMT, hstmt_, "SQLBindParameter(uint64)");
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
            } else if constexpr (std::is_same_v<T, std::wstring>) {
                auto sqlwchars = detail::odbc::wstring_to_sqlwchar(val);
                store.c_type = SQL_C_WCHAR;
                store.sql_type = SQL_WVARCHAR;
                store.col_size = val.empty() ? 1 : static_cast<SQLULEN>(val.size());
                store.dec_digits = 0;
                store.ind = static_cast<SQLLEN>((sqlwchars.size() - 1) * sizeof(SQLWCHAR));
                store.buffer.resize(sqlwchars.size() * sizeof(SQLWCHAR));
                std::memcpy(store.buffer.data(), sqlwchars.data(), sqlwchars.size() * sizeof(SQLWCHAR));
                SQLRETURN rc = SQLBindParameter(hstmt_, param_num, SQL_PARAM_INPUT, store.c_type, store.sql_type,
                                                store.col_size, store.dec_digits, store.buffer.data(), static_cast<SQLLEN>(store.buffer.size()), &store.ind);
                check_rc(rc, SQL_HANDLE_STMT, hstmt_, "SQLBindParameter(wstring)");
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
            } else if constexpr (std::is_same_v<T, SqlNumeric>) {
                std::string s = val.to_string();
                store.c_type = SQL_C_CHAR;
                store.sql_type = SQL_NUMERIC;
                store.col_size = val.precision > 0 ? val.precision : 28;
                store.dec_digits = val.scale >= 0 ? val.scale : 6;
                store.ind = static_cast<SQLLEN>(s.size());
                store.buffer.resize(s.size() + 1);
                std::memcpy(store.buffer.data(), s.c_str(), s.size() + 1);
                SQLRETURN rc = SQLBindParameter(hstmt_, param_num, SQL_PARAM_INPUT, store.c_type, store.sql_type,
                                                store.col_size, store.dec_digits, store.buffer.data(), static_cast<SQLLEN>(store.buffer.size()), &store.ind);
                check_rc(rc, SQL_HANDLE_STMT, hstmt_, "SQLBindParameter(numeric)");
            } else if constexpr (std::is_same_v<T, SqlDate>) {
                store.c_type = SQL_C_TYPE_DATE;
                store.sql_type = SQL_TYPE_DATE;
                store.col_size = 10;
                store.dec_digits = 0;
                store.ind = sizeof(DATE_STRUCT);
                store.buffer.resize(sizeof(DATE_STRUCT));
                DATE_STRUCT ds{val.year, val.month, val.day};
                std::memcpy(store.buffer.data(), &ds, sizeof(DATE_STRUCT));
                SQLRETURN rc = SQLBindParameter(hstmt_, param_num, SQL_PARAM_INPUT, store.c_type, store.sql_type,
                                                store.col_size, store.dec_digits, store.buffer.data(), sizeof(DATE_STRUCT), &store.ind);
                check_rc(rc, SQL_HANDLE_STMT, hstmt_, "SQLBindParameter(date)");
            } else if constexpr (std::is_same_v<T, SqlTime>) {
                std::string s = val.to_string();
                store.c_type = SQL_C_CHAR;
                store.sql_type = SQL_VARCHAR;
                store.col_size = 20;
                store.dec_digits = 0;
                store.ind = static_cast<SQLLEN>(s.size());
                store.buffer.resize(s.size() + 1);
                std::memcpy(store.buffer.data(), s.c_str(), s.size() + 1);
                SQLRETURN rc = SQLBindParameter(hstmt_, param_num, SQL_PARAM_INPUT, store.c_type, store.sql_type,
                                                store.col_size, store.dec_digits, store.buffer.data(), static_cast<SQLLEN>(store.buffer.size()), &store.ind);
                check_rc(rc, SQL_HANDLE_STMT, hstmt_, "SQLBindParameter(time)");
            } else if constexpr (std::is_same_v<T, SqlTimestamp>) {
                store.c_type = SQL_C_TYPE_TIMESTAMP;
                store.sql_type = SQL_TYPE_TIMESTAMP;
                store.col_size = 27;
                store.dec_digits = 6;
                store.ind = sizeof(TIMESTAMP_STRUCT);
                store.buffer.resize(sizeof(TIMESTAMP_STRUCT));
                TIMESTAMP_STRUCT ts{val.year, val.month, val.day, val.hour, val.minute, val.second, val.fraction};
                std::memcpy(store.buffer.data(), &ts, sizeof(TIMESTAMP_STRUCT));
                SQLRETURN rc = SQLBindParameter(hstmt_, param_num, SQL_PARAM_INPUT, store.c_type, store.sql_type,
                                                store.col_size, store.dec_digits, store.buffer.data(), sizeof(TIMESTAMP_STRUCT), &store.ind);
                check_rc(rc, SQL_HANDLE_STMT, hstmt_, "SQLBindParameter(timestamp)");
            } else if constexpr (std::is_same_v<T, SqlInterval>) {
                std::string s = val.to_string();
                store.c_type = SQL_C_CHAR;
                store.sql_type = SQL_VARCHAR;
                store.col_size = static_cast<SQLULEN>(s.size() > 0 ? s.size() : 1);
                store.dec_digits = 0;
                store.ind = static_cast<SQLLEN>(s.size());
                store.buffer.resize(s.size() + 1);
                std::memcpy(store.buffer.data(), s.c_str(), s.size() + 1);
                SQLRETURN rc = SQLBindParameter(hstmt_, param_num, SQL_PARAM_INPUT, store.c_type, store.sql_type,
                                                store.col_size, store.dec_digits, store.buffer.data(), static_cast<SQLLEN>(store.buffer.size()), &store.ind);
                check_rc(rc, SQL_HANDLE_STMT, hstmt_, "SQLBindParameter(interval)");
            } else if constexpr (std::is_same_v<T, SqlGuid>) {
                std::string s = val.to_string();
                store.c_type = SQL_C_CHAR;
                store.sql_type = SQL_VARCHAR;
                store.col_size = 36;
                store.dec_digits = 0;
                store.ind = static_cast<SQLLEN>(s.size());
                store.buffer.resize(s.size() + 1);
                std::memcpy(store.buffer.data(), s.c_str(), s.size() + 1);
                SQLRETURN rc = SQLBindParameter(hstmt_, param_num, SQL_PARAM_INPUT, store.c_type, store.sql_type,
                                                store.col_size, store.dec_digits, store.buffer.data(), static_cast<SQLLEN>(store.buffer.size()), &store.ind);
                check_rc(rc, SQL_HANDLE_STMT, hstmt_, "SQLBindParameter(guid)");
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

    std::string effective_conn_str = connection_string_;
    if (effective_conn_str.find('=') == std::string::npos && !effective_conn_str.empty()) {
        effective_conn_str = "DSN=" + effective_conn_str + ";";
    }

    SQLCHAR conn_out[1024];
    SQLSMALLINT out_len = 0;
    rc = SQLDriverConnectA(
        hdbc_,
        nullptr,
        reinterpret_cast<SQLCHAR*>(const_cast<char*>(effective_conn_str.data())),
        static_cast<SQLSMALLINT>(effective_conn_str.size()),
        conn_out,
        sizeof(conn_out),
        &out_len,
        SQL_DRIVER_NOPROMPT
    );

    if (!SQL_SUCCEEDED(rc)) {
        std::string last_err = get_odbc_error(SQL_HANDLE_DBC, hdbc_);
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

std::unique_ptr<IDataReader> OdbcConnection::execute_query_direct(std::string_view sql) {
    if (!is_open_) {
        throw DbException("Cannot execute query: " + get_driver_display_name() + " connection is not open");
    }
    SQLHSTMT hstmt = SQL_NULL_HSTMT;
    SQLRETURN rc = SQLAllocHandle(SQL_HANDLE_STMT, hdbc_, &hstmt);
    check_rc(rc, SQL_HANDLE_DBC, hdbc_, "SQLAllocHandle(SQL_HANDLE_STMT)");

    rc = SQLExecDirectA(hstmt, reinterpret_cast<SQLCHAR*>(const_cast<char*>(sql.data())), static_cast<SQLINTEGER>(sql.size()));
    if (!SQL_SUCCEEDED(rc) && rc != SQL_NO_DATA) {
        std::string err = get_odbc_error(SQL_HANDLE_STMT, hstmt);
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        throw DbException(get_driver_display_name() + " execute_query_direct failed: " + err);
    }
    return std::make_unique<OdbcDataReader>(hstmt, true);
}

size_t OdbcConnection::execute_non_query_direct(std::string_view sql) {
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
        throw DbException(get_driver_display_name() + " execute_non_query_direct failed: " + err);
    }
    SQLLEN row_count = 0;
    SQLRowCount(hstmt, &row_count);
    SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
    return row_count > 0 ? static_cast<size_t>(row_count) : 0;
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

#endif // defined(CPPLINQ_HAS_MSSQL) || defined(CPPLINQ_HAS_MYSQL) || defined(CPPLINQ_HAS_POSTGRES) || defined(CPPLINQ_HAS_INFORMIX) || defined(CPPLINQ_HAS_ORACLE)
