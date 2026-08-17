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

namespace cpplinq::detail::odbc {

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
        std::vector<int64_t>  int_data;
        std::vector<double>   dbl_data;
        std::vector<char>     str_data;
        std::vector<uint8_t>  blob_data;
        std::vector<uint8_t>  bit_data;
        std::vector<SQLLEN>   indicators;
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
                }
            }, flat_params[r * col_count + c]);
        }
    }

    // Pass 3: Bind parameters
    for (size_t c = 0; c < col_count; ++c) {
        auto& ca = cols[c];
        void* ptr = nullptr;
        switch (ca.c_type) {
            case SQL_C_SBIGINT: ptr = ca.int_data.data();  break;
            case SQL_C_DOUBLE:  ptr = ca.dbl_data.data();  break;
            case SQL_C_BIT:     ptr = ca.bit_data.data();  break;
            case SQL_C_CHAR:    ptr = ca.str_data.data();  break;
            case SQL_C_BINARY:  ptr = ca.blob_data.data(); break;
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
