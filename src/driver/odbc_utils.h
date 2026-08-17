#pragma once

#if defined(CPPLINQ_HAS_MSSQL) || defined(CPPLINQ_HAS_MYSQL) || defined(CPPLINQ_HAS_POSTGRES)

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
#include <algorithm>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>
#include <cctype>

namespace cpplinq::detail::odbc {

#ifndef SQL_SS_TIME2
#define SQL_SS_TIME2 (-154)
#endif

#ifndef SQL_SS_TIMESTAMPOFFSET
#define SQL_SS_TIMESTAMPOFFSET (-155)
#endif

inline std::string get_odbc_error(SQLSMALLINT handle_type, SQLHANDLE handle) {
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

inline void check_rc(SQLRETURN rc, SQLSMALLINT handle_type, SQLHANDLE handle, const char* context) {
    if (!SQL_SUCCEEDED(rc) && rc != SQL_NO_DATA) {
        std::string err = get_odbc_error(handle_type, handle);
        throw DbException(std::string(context) + ": " + err);
    }
}

// ----------------------------------------------------------------------------
// SQL_NUMERIC_STRUCT conversion helpers
// ----------------------------------------------------------------------------

inline std::string numeric_struct_to_string(const SQL_NUMERIC_STRUCT& ns) {
    uint8_t bytes[SQL_MAX_NUMERIC_LEN];
    std::memcpy(bytes, ns.val, SQL_MAX_NUMERIC_LEN);
    
    bool all_zero = true;
    for (int i = 0; i < SQL_MAX_NUMERIC_LEN; ++i) {
        if (bytes[i] != 0) { all_zero = false; break; }
    }
    if (all_zero) {
        if (ns.scale <= 0) return "0";
        return "0." + std::string(static_cast<size_t>(ns.scale), '0');
    }

    std::string digits;
    while (true) {
        bool zero = true;
        uint32_t remainder = 0;
        for (int i = SQL_MAX_NUMERIC_LEN - 1; i >= 0; --i) {
            uint32_t cur = (remainder << 8) | bytes[i];
            bytes[i] = static_cast<uint8_t>(cur / 10);
            remainder = cur % 10;
            if (bytes[i] != 0) zero = false;
        }
        digits.push_back(static_cast<char>('0' + remainder));
        if (zero) break;
    }
    std::reverse(digits.begin(), digits.end());

    if (ns.scale > 0) {
        if (digits.size() <= static_cast<size_t>(ns.scale)) {
            std::string prefix(static_cast<size_t>(ns.scale) + 1 - digits.size(), '0');
            digits = prefix + digits;
        }
        size_t dot_pos = digits.size() - static_cast<size_t>(ns.scale);
        digits.insert(dot_pos, ".");
    }
    if (ns.sign == 0) {
        digits.insert(digits.begin(), '-');
    }
    return digits;
}

inline SQL_NUMERIC_STRUCT string_to_numeric_struct(std::string_view str, uint8_t default_prec = 0, int8_t default_scale = -1) {
    SQL_NUMERIC_STRUCT ns{};
    std::memset(&ns, 0, sizeof(ns));
    ns.sign = 1;

    std::string s(str);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    if (s.empty()) return ns;

    if (s.front() == '-') {
        ns.sign = 0;
        s.erase(s.begin());
    } else if (s.front() == '+') {
        s.erase(s.begin());
    }

    size_t dot = s.find('.');
    int8_t scale = 0;
    std::string digits;
    if (dot != std::string::npos) {
        digits = s.substr(0, dot) + s.substr(dot + 1);
        scale = static_cast<int8_t>(s.size() - dot - 1);
    } else {
        digits = s;
        scale = 0;
    }

    if (default_scale >= 0) {
        while (scale < default_scale) {
            digits.push_back('0');
            scale++;
        }
        while (scale > default_scale && !digits.empty()) {
            digits.pop_back();
            scale--;
        }
    }

    ns.scale = scale;
    ns.precision = default_prec > 0 ? default_prec : static_cast<uint8_t>(std::max<size_t>(1, digits.size()));

    for (char c : digits) {
        if (c >= '0' && c <= '9') {
            uint32_t digit = c - '0';
            uint32_t carry = digit;
            for (int i = 0; i < SQL_MAX_NUMERIC_LEN; ++i) {
                uint32_t cur = static_cast<uint32_t>(ns.val[i]) * 10 + carry;
                ns.val[i] = static_cast<uint8_t>(cur & 0xFF);
                carry = cur >> 8;
            }
        }
    }
    return ns;
}

// ----------------------------------------------------------------------------
// SQL_INTERVAL_STRUCT conversion helpers
// ----------------------------------------------------------------------------

inline SQL_INTERVAL_STRUCT interval_to_odbc_struct(const SqlInterval& iv) {
    SQL_INTERVAL_STRUCT s{};
    std::memset(&s, 0, sizeof(s));
    s.interval_sign = iv.is_negative ? SQL_TRUE : SQL_FALSE;
    switch (iv.type) {
        case IntervalType::Year:
            s.interval_type = SQL_IS_YEAR;
            s.intval.year_month.year = iv.years;
            break;
        case IntervalType::Month:
            s.interval_type = SQL_IS_MONTH;
            s.intval.year_month.month = iv.months;
            break;
        case IntervalType::YearToMonth:
            s.interval_type = SQL_IS_YEAR_TO_MONTH;
            s.intval.year_month.year = iv.years;
            s.intval.year_month.month = iv.months;
            break;
        case IntervalType::Day:
            s.interval_type = SQL_IS_DAY;
            s.intval.day_second.day = iv.days;
            break;
        case IntervalType::Hour:
            s.interval_type = SQL_IS_HOUR;
            s.intval.day_second.hour = iv.hours;
            break;
        case IntervalType::Minute:
            s.interval_type = SQL_IS_MINUTE;
            s.intval.day_second.minute = iv.minutes;
            break;
        case IntervalType::Second:
            s.interval_type = SQL_IS_SECOND;
            s.intval.day_second.second = iv.seconds;
            s.intval.day_second.fraction = iv.fraction;
            break;
        case IntervalType::DayToHour:
            s.interval_type = SQL_IS_DAY_TO_HOUR;
            s.intval.day_second.day = iv.days;
            s.intval.day_second.hour = iv.hours;
            break;
        case IntervalType::DayToMinute:
            s.interval_type = SQL_IS_DAY_TO_MINUTE;
            s.intval.day_second.day = iv.days;
            s.intval.day_second.hour = iv.hours;
            s.intval.day_second.minute = iv.minutes;
            break;
        case IntervalType::DayToSecond:
        default:
            s.interval_type = SQL_IS_DAY_TO_SECOND;
            s.intval.day_second.day = iv.days;
            s.intval.day_second.hour = iv.hours;
            s.intval.day_second.minute = iv.minutes;
            s.intval.day_second.second = iv.seconds;
            s.intval.day_second.fraction = iv.fraction;
            break;
    }
    return s;
}

inline SqlInterval odbc_struct_to_interval(const SQL_INTERVAL_STRUCT& s) {
    SqlInterval iv;
    iv.is_negative = (s.interval_sign == SQL_TRUE);
    switch (s.interval_type) {
        case SQL_IS_YEAR:
            iv.type = IntervalType::Year;
            iv.years = s.intval.year_month.year;
            break;
        case SQL_IS_MONTH:
            iv.type = IntervalType::Month;
            iv.months = s.intval.year_month.month;
            break;
        case SQL_IS_YEAR_TO_MONTH:
            iv.type = IntervalType::YearToMonth;
            iv.years = s.intval.year_month.year;
            iv.months = s.intval.year_month.month;
            break;
        case SQL_IS_DAY:
            iv.type = IntervalType::Day;
            iv.days = s.intval.day_second.day;
            break;
        case SQL_IS_HOUR:
            iv.type = IntervalType::Hour;
            iv.hours = s.intval.day_second.hour;
            break;
        case SQL_IS_MINUTE:
            iv.type = IntervalType::Minute;
            iv.minutes = s.intval.day_second.minute;
            break;
        case SQL_IS_SECOND:
            iv.type = IntervalType::Second;
            iv.seconds = s.intval.day_second.second;
            iv.fraction = s.intval.day_second.fraction;
            break;
        case SQL_IS_DAY_TO_HOUR:
            iv.type = IntervalType::DayToHour;
            iv.days = s.intval.day_second.day;
            iv.hours = s.intval.day_second.hour;
            break;
        case SQL_IS_DAY_TO_MINUTE:
            iv.type = IntervalType::DayToMinute;
            iv.days = s.intval.day_second.day;
            iv.hours = s.intval.day_second.hour;
            iv.minutes = s.intval.day_second.minute;
            break;
        case SQL_IS_DAY_TO_SECOND:
        default:
            iv.type = IntervalType::DayToSecond;
            iv.days = s.intval.day_second.day;
            iv.hours = s.intval.day_second.hour;
            iv.minutes = s.intval.day_second.minute;
            iv.seconds = s.intval.day_second.second;
            iv.fraction = s.intval.day_second.fraction;
            break;
    }
    return iv;
}

inline size_t execute_insert_many_batch(
    SQLHDBC hdbc,
    std::string_view sql,
    const std::vector<BoundValue>& flat_params,
    size_t col_count,
    size_t row_count,
    const char* driver_prefix
) {
    if (row_count == 0 || col_count == 0) return 0;

    SQLHSTMT hstmt = SQL_NULL_HSTMT;
    SQLRETURN rc = SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt);
    check_rc(rc, SQL_HANDLE_DBC, hdbc, (std::string(driver_prefix) + "::insert_many_batch: SQLAllocHandle").c_str());

    struct StmtGuard {
        SQLHSTMT handle;
        ~StmtGuard() {
            if (handle != SQL_NULL_HSTMT) {
                SQLSetStmtAttr(handle, SQL_ATTR_PARAMSET_SIZE, reinterpret_cast<SQLPOINTER>(1), SQL_IS_UINTEGER);
                SQLFreeHandle(SQL_HANDLE_STMT, handle);
            }
        }
    } guard{hstmt};

    rc = SQLPrepareA(
        hstmt,
        reinterpret_cast<SQLCHAR*>(const_cast<char*>(sql.data())),
        static_cast<SQLINTEGER>(sql.size())
    );
    check_rc(rc, SQL_HANDLE_STMT, hstmt, (std::string(driver_prefix) + "::insert_many_batch: SQLPrepare").c_str());

    rc = SQLSetStmtAttr(
        hstmt,
        SQL_ATTR_PARAMSET_SIZE,
        reinterpret_cast<SQLPOINTER>(static_cast<uintptr_t>(row_count)),
        SQL_IS_UINTEGER
    );
    check_rc(rc, SQL_HANDLE_STMT, hstmt, (std::string(driver_prefix) + "::insert_many_batch: SQL_ATTR_PARAMSET_SIZE").c_str());

    struct ColArrays {
        std::vector<int64_t>             int_data;
        std::vector<uint64_t>            uint_data;
        std::vector<double>              dbl_data;
        std::vector<char>                str_data;
        std::vector<uint8_t>             blob_data;
        std::vector<uint8_t>             bit_data;
        std::vector<SQL_NUMERIC_STRUCT>  num_data;
        std::vector<DATE_STRUCT>         date_data;
        std::vector<TIME_STRUCT>         time_data;
        std::vector<TIMESTAMP_STRUCT>    ts_data;
        std::vector<SQL_INTERVAL_STRUCT> interval_data;
        std::vector<SQLLEN>              indicators;
        SQLSMALLINT c_type = 0;
        SQLSMALLINT sql_type = 0;
        SQLSMALLINT dec_digits = 0;
        SQLULEN     col_size = 0;
        SQLLEN      buf_stride = 0;
    };
    std::vector<ColArrays> cols(col_count);

    // Pass 1: Determine types and strides per column
    for (size_t c = 0; c < col_count; ++c) {
        auto& ca = cols[c];
        ca.indicators.assign(row_count, 0);
        SQLSMALLINT ct = SQL_C_CHAR, st = SQL_VARCHAR;
        size_t max_len = 1;

        for (size_t r = 0; r < row_count; ++r) {
            std::visit([&](const auto& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, int64_t>) {
                    ct = SQL_C_SBIGINT; st = SQL_BIGINT;
                } else if constexpr (std::is_same_v<T, uint64_t>) {
                    ct = SQL_C_UBIGINT; st = SQL_BIGINT;
                } else if constexpr (std::is_same_v<T, double>) {
                    ct = SQL_C_DOUBLE; st = SQL_DOUBLE;
                } else if constexpr (std::is_same_v<T, bool>) {
                    ct = SQL_C_BIT; st = SQL_BIT;
                } else if constexpr (std::is_same_v<T, std::string>) {
                    ct = SQL_C_CHAR; st = SQL_VARCHAR;
                    max_len = std::max(max_len, v.size() + 1);
                } else if constexpr (std::is_same_v<T, std::vector<uint8_t>>) {
                    ct = SQL_C_BINARY; st = SQL_VARBINARY;
                    max_len = std::max(max_len, v.size() > 0 ? v.size() : 1);
                } else if constexpr (std::is_same_v<T, SqlNumeric>) {
                    ct = SQL_C_NUMERIC; st = SQL_NUMERIC;
                } else if constexpr (std::is_same_v<T, SqlDate>) {
                    ct = SQL_C_TYPE_DATE; st = SQL_TYPE_DATE;
                } else if constexpr (std::is_same_v<T, SqlTime>) {
                    ct = SQL_C_TYPE_TIME; st = SQL_TYPE_TIME;
                } else if constexpr (std::is_same_v<T, SqlTimestamp>) {
                    ct = SQL_C_TYPE_TIMESTAMP; st = SQL_TYPE_TIMESTAMP;
                } else if constexpr (std::is_same_v<T, SqlInterval>) {
                    ct = SQL_C_INTERVAL_DAY_TO_SECOND; st = SQL_INTERVAL_DAY_TO_SECOND;
                }
            }, flat_params[r * col_count + c]);
        }

        ca.c_type = ct;
        ca.sql_type = st;
        switch (ct) {
            case SQL_C_SBIGINT:
                ca.int_data.resize(row_count, 0);
                ca.buf_stride = sizeof(int64_t);
                ca.col_size = 19;
                break;
            case SQL_C_UBIGINT:
                ca.uint_data.resize(row_count, 0);
                ca.buf_stride = sizeof(uint64_t);
                ca.col_size = 20;
                break;
            case SQL_C_DOUBLE:
                ca.dbl_data.resize(row_count, 0.0);
                ca.buf_stride = sizeof(double);
                ca.col_size = 53;
                break;
            case SQL_C_BIT:
                ca.bit_data.resize(row_count, 0);
                ca.buf_stride = 1;
                ca.col_size = 1;
                break;
            case SQL_C_CHAR:
                ca.str_data.resize(row_count * max_len, 0);
                ca.buf_stride = max_len;
                ca.col_size = max_len > 1 ? max_len - 1 : 1;
                break;
            case SQL_C_BINARY:
                ca.blob_data.resize(row_count * max_len, 0);
                ca.buf_stride = max_len;
                ca.col_size = max_len;
                break;
            case SQL_C_NUMERIC:
                ca.num_data.resize(row_count);
                ca.buf_stride = sizeof(SQL_NUMERIC_STRUCT);
                ca.col_size = 28;
                ca.dec_digits = 6;
                break;
            case SQL_C_TYPE_DATE:
                ca.date_data.resize(row_count);
                ca.buf_stride = sizeof(DATE_STRUCT);
                ca.col_size = 10;
                break;
            case SQL_C_TYPE_TIME:
                ca.time_data.resize(row_count);
                ca.buf_stride = sizeof(TIME_STRUCT);
                ca.col_size = 8;
                break;
            case SQL_C_TYPE_TIMESTAMP:
                ca.ts_data.resize(row_count);
                ca.buf_stride = sizeof(TIMESTAMP_STRUCT);
                ca.col_size = 27;
                ca.dec_digits = 6;
                break;
            case SQL_C_INTERVAL_DAY_TO_SECOND:
                ca.interval_data.resize(row_count);
                ca.buf_stride = sizeof(SQL_INTERVAL_STRUCT);
                ca.col_size = 30;
                ca.dec_digits = 6;
                break;
        }
    }

    // Pass 2: Fill arrays
    for (size_t r = 0; r < row_count; ++r) {
        for (size_t c = 0; c < col_count; ++c) {
            auto& ca = cols[c];
            std::visit([&](const auto& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, std::monostate>) {
                    ca.indicators[r] = SQL_NULL_DATA;
                } else if constexpr (std::is_same_v<T, int64_t>) {
                    ca.int_data[r] = v;
                    ca.indicators[r] = sizeof(int64_t);
                } else if constexpr (std::is_same_v<T, uint64_t>) {
                    ca.uint_data[r] = v;
                    ca.indicators[r] = sizeof(uint64_t);
                } else if constexpr (std::is_same_v<T, double>) {
                    ca.dbl_data[r] = v;
                    ca.indicators[r] = sizeof(double);
                } else if constexpr (std::is_same_v<T, bool>) {
                    ca.bit_data[r] = v ? 1 : 0;
                    ca.indicators[r] = 1;
                } else if constexpr (std::is_same_v<T, std::string>) {
                    char* dst = ca.str_data.data() + r * ca.buf_stride;
                    std::memcpy(dst, v.c_str(), v.size());
                    dst[v.size()] = '\0';
                    ca.indicators[r] = static_cast<SQLLEN>(v.size());
                } else if constexpr (std::is_same_v<T, std::vector<uint8_t>>) {
                    uint8_t* dst = ca.blob_data.data() + r * ca.buf_stride;
                    if (!v.empty()) std::memcpy(dst, v.data(), v.size());
                    ca.indicators[r] = static_cast<SQLLEN>(v.size());
                } else if constexpr (std::is_same_v<T, SqlNumeric>) {
                    ca.num_data[r] = string_to_numeric_struct(v.to_string(), 28, 6);
                    ca.indicators[r] = sizeof(SQL_NUMERIC_STRUCT);
                } else if constexpr (std::is_same_v<T, SqlDate>) {
                    ca.date_data[r] = DATE_STRUCT{v.year, v.month, v.day};
                    ca.indicators[r] = sizeof(DATE_STRUCT);
                } else if constexpr (std::is_same_v<T, SqlTime>) {
                    ca.time_data[r] = TIME_STRUCT{v.hour, v.minute, v.second};
                    ca.indicators[r] = sizeof(TIME_STRUCT);
                } else if constexpr (std::is_same_v<T, SqlTimestamp>) {
                    ca.ts_data[r] = TIMESTAMP_STRUCT{v.year, v.month, v.day, v.hour, v.minute, v.second, v.fraction};
                    ca.indicators[r] = sizeof(TIMESTAMP_STRUCT);
                } else if constexpr (std::is_same_v<T, SqlInterval>) {
                    ca.interval_data[r] = interval_to_odbc_struct(v);
                    ca.indicators[r] = sizeof(SQL_INTERVAL_STRUCT);
                }
            }, flat_params[r * col_count + c]);
        }
    }

    // Pass 3: Bind parameters
    for (size_t c = 0; c < col_count; ++c) {
        auto& ca = cols[c];
        void* ptr = nullptr;
        switch (ca.c_type) {
            case SQL_C_SBIGINT:                   ptr = ca.int_data.data();      break;
            case SQL_C_UBIGINT:                   ptr = ca.uint_data.data();     break;
            case SQL_C_DOUBLE:                    ptr = ca.dbl_data.data();      break;
            case SQL_C_BIT:                       ptr = ca.bit_data.data();      break;
            case SQL_C_CHAR:                      ptr = ca.str_data.data();      break;
            case SQL_C_BINARY:                    ptr = ca.blob_data.data();     break;
            case SQL_C_NUMERIC:                   ptr = ca.num_data.data();      break;
            case SQL_C_TYPE_DATE:                 ptr = ca.date_data.data();     break;
            case SQL_C_TYPE_TIME:                 ptr = ca.time_data.data();     break;
            case SQL_C_TYPE_TIMESTAMP:            ptr = ca.ts_data.data();       break;
            case SQL_C_INTERVAL_DAY_TO_SECOND:    ptr = ca.interval_data.data(); break;
        }

        rc = SQLBindParameter(
            hstmt,
            static_cast<SQLUSMALLINT>(c + 1),
            SQL_PARAM_INPUT,
            ca.c_type,
            ca.sql_type,
            ca.col_size,
            ca.dec_digits,
            ptr,
            ca.buf_stride,
            ca.indicators.data()
        );
        check_rc(rc, SQL_HANDLE_STMT, hstmt, (std::string(driver_prefix) + "::insert_many_batch: SQLBindParameter").c_str());
    }

    // Pass 4: Execute
    rc = SQLExecute(hstmt);
    check_rc(rc, SQL_HANDLE_STMT, hstmt, (std::string(driver_prefix) + "::insert_many_batch: SQLExecute").c_str());

    SQLLEN affected = -1;
    SQLRowCount(hstmt, &affected);
    return affected >= 0 ? static_cast<size_t>(affected) : row_count;
}

} // namespace cpplinq::detail::odbc

#endif
