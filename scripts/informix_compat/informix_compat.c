/**
 * Informix ODBC Linux 64-bit Compatibility Shim for unixODBC
 *
 * Problem:
 * 1. unixODBC on Linux is compiled with sizeof(SQLWCHAR) == 2 (UTF-16 unsigned short).
 *    IBM Informix Client SDK ODBC driver (iclit09b.so) on Linux defines SQLWCHAR as wchar_t
 *    (UTF-32, 4 bytes).
 * 2. When unixODBC extracts diagnostic records via SQLGetDiagRecW, it allocates a 12-byte
 *    buffer (6 * sizeof(unsigned short)) on its stack. Informix's SQLGetDiagRecW writes 24 bytes
 *    (6 * sizeof(wchar_t)), immediately corrupting the stack canary and causing a crash:
 *    "*** stack smashing detected ***: terminated".
 * 3. When binding parameters or columns with SQL_C_WCHAR, cpplinq passes UTF-16, but Informix
 *    expects UTF-32 wchar_t, leading to codeset conversion errors (HY000 / native 21000).
 *
 * Solution:
 * Intercept lt_dlsym calls targeting the Informix driver:
 * - SQLGetDiagRecW: translates via Informix's ANSI SQLGetDiagRec and returns 16-bit SQLWCHAR.
 * - SQLBindParameter: converts 16-bit SQLWCHAR buffers to 32-bit wchar_t for Informix.
 * - SQLBindCol & SQLFetch: converts 32-bit wchar_t results back to 16-bit SQLWCHAR buffers.
 */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <sql.h>
#include <sqlext.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

typedef SQLRETURN (*real_SQLGetDiagRec_t)(
    SQLSMALLINT HandleType,
    SQLHANDLE Handle,
    SQLSMALLINT RecNumber,
    SQLCHAR *SQLState,
    SQLINTEGER *NativeErrorPtr,
    SQLCHAR *MessageText,
    SQLSMALLINT BufferLength,
    SQLSMALLINT *TextLengthPtr);

typedef SQLRETURN (*real_SQLBindParameter_t)(
    SQLHSTMT StatementHandle,
    SQLUSMALLINT ParameterNumber,
    SQLSMALLINT InputOutputType,
    SQLSMALLINT ValueType,
    SQLSMALLINT ParameterType,
    SQLULEN ColumnSize,
    SQLSMALLINT DecimalDigits,
    SQLPOINTER ParameterValuePtr,
    SQLLEN BufferLength,
    SQLLEN *StrLen_or_IndPtr);

typedef SQLRETURN (*real_SQLBindCol_t)(
    SQLHSTMT StatementHandle,
    SQLUSMALLINT ColumnNumber,
    SQLSMALLINT TargetType,
    SQLPOINTER TargetValuePtr,
    SQLLEN BufferLength,
    SQLLEN *StrLen_or_Ind);

typedef SQLRETURN (*real_SQLFetch_t)(SQLHSTMT StatementHandle);

static void *get_driver_handle(void) {
    static void *h = NULL;
    if (!h) {
        const char *informixdir = getenv("INFORMIXDIR");
        char path[1024];
        if (informixdir && informixdir[0]) {
            snprintf(path, sizeof(path), "%s/lib/cli/iclit09b.so", informixdir);
            h = dlopen(path, RTLD_NOLOAD | RTLD_NOW);
            if (!h) h = dlopen(path, RTLD_GLOBAL | RTLD_NOW);
        }
        if (!h) {
            h = dlopen("iclit09b.so", RTLD_NOLOAD | RTLD_NOW);
            if (!h) h = dlopen("iclit09b.so", RTLD_GLOBAL | RTLD_NOW);
        }
    }
    return h;
}

static void *get_driver_sym(const char *name) {
    void *h = get_driver_handle();
    if (!h) return NULL;
    void *(*real_dlsym_fn)(void *, const char *) = (void *(*)(void *, const char *))dlvsym(RTLD_NEXT, "dlsym", "GLIBC_2.2.5");
    if (!real_dlsym_fn) {
        real_dlsym_fn = (void *(*)(void *, const char *))dlvsym(RTLD_NEXT, "dlsym", "GLIBC_2.34");
    }
    if (real_dlsym_fn) {
        return real_dlsym_fn(h, name);
    }
    return dlsym(h, name);
}

SQLRETURN my_SQLGetDiagRecW(
    SQLSMALLINT HandleType,
    SQLHANDLE Handle,
    SQLSMALLINT RecNumber,
    SQLWCHAR *SQLState,
    SQLINTEGER *NativeErrorPtr,
    SQLWCHAR *MessageText,
    SQLSMALLINT BufferLength,
    SQLSMALLINT *TextLengthPtr)
{
    static real_SQLGetDiagRec_t real_fn = NULL;
    if (!real_fn) {
        real_fn = (real_SQLGetDiagRec_t)get_driver_sym("SQLGetDiagRec");
    }
    if (!real_fn) return SQL_NO_DATA;

    SQLCHAR sqlstate_a[16] = {0};
    SQLCHAR msg_a[2048] = {0};
    SQLSMALLINT msg_len = 0;

    SQLRETURN rc = real_fn(HandleType, Handle, RecNumber, sqlstate_a, NativeErrorPtr, msg_a, sizeof(msg_a), &msg_len);
    if (SQL_SUCCEEDED(rc)) {
        if (SQLState) {
            unsigned short *ws = (unsigned short *)SQLState;
            for (int i = 0; i < 5 && sqlstate_a[i]; i++) {
                ws[i] = (unsigned short)sqlstate_a[i];
            }
            ws[5] = 0;
        }
        if (MessageText && BufferLength > 0) {
            unsigned short *wm = (unsigned short *)MessageText;
            int n = (msg_len < BufferLength - 1) ? msg_len : (BufferLength - 1);
            for (int i = 0; i < n; i++) {
                wm[i] = (unsigned short)(unsigned char)msg_a[i];
            }
            wm[n] = 0;
            if (TextLengthPtr) *TextLengthPtr = n;
        }
    }
    return rc;
}

#define MAX_CONV 64
static struct {
    SQLHSTMT stmt;
    SQLUSMALLINT param;
    wchar_t *wbuf;
    SQLLEN ind;
} conv_params[MAX_CONV];
static int conv_count = 0;

SQLRETURN my_SQLBindParameter(
    SQLHSTMT StatementHandle,
    SQLUSMALLINT ParameterNumber,
    SQLSMALLINT InputOutputType,
    SQLSMALLINT ValueType,
    SQLSMALLINT ParameterType,
    SQLULEN ColumnSize,
    SQLSMALLINT DecimalDigits,
    SQLPOINTER ParameterValuePtr,
    SQLLEN BufferLength,
    SQLLEN *StrLen_or_IndPtr)
{
    static real_SQLBindParameter_t real_fn = NULL;
    if (!real_fn) {
        real_fn = (real_SQLBindParameter_t)get_driver_sym("SQLBindParameter");
    }
    if (!real_fn) return SQL_ERROR;

    if (ValueType == SQL_C_WCHAR && ParameterValuePtr && StrLen_or_IndPtr) {
        SQLLEN ind = *StrLen_or_IndPtr;
        if (ind >= 0) {
            size_t nchars = ind / sizeof(unsigned short);
            const unsigned short *u16 = (const unsigned short *)ParameterValuePtr;
            
            int slot = -1;
            for (int i = 0; i < conv_count; i++) {
                if (conv_params[i].stmt == StatementHandle && conv_params[i].param == ParameterNumber) {
                    slot = i;
                    break;
                }
            }
            if (slot < 0 && conv_count < MAX_CONV) {
                slot = conv_count++;
            }
            if (slot >= 0) {
                free(conv_params[slot].wbuf);
                conv_params[slot].stmt = StatementHandle;
                conv_params[slot].param = ParameterNumber;
                conv_params[slot].wbuf = (wchar_t *)malloc((nchars + 1) * sizeof(wchar_t));
                for (size_t i = 0; i < nchars; i++) {
                    conv_params[slot].wbuf[i] = (wchar_t)u16[i];
                }
                conv_params[slot].wbuf[nchars] = 0;
                conv_params[slot].ind = nchars * sizeof(wchar_t);

                return real_fn(StatementHandle, ParameterNumber, InputOutputType,
                               SQL_C_WCHAR, ParameterType, ColumnSize, DecimalDigits,
                               conv_params[slot].wbuf, (nchars + 1) * sizeof(wchar_t),
                               &conv_params[slot].ind);
            }
        }
    }

    return real_fn(StatementHandle, ParameterNumber, InputOutputType,
                   ValueType, ParameterType, ColumnSize, DecimalDigits,
                   ParameterValuePtr, BufferLength, StrLen_or_IndPtr);
}

static struct {
    SQLHSTMT stmt;
    SQLUSMALLINT col;
    SQLPOINTER app_target;
    SQLLEN app_buflen;
    SQLLEN *app_ind;
    wchar_t *driver_target;
    SQLLEN driver_buflen;
    SQLLEN driver_ind;
} conv_cols[MAX_CONV];
static int conv_col_count = 0;

SQLRETURN my_SQLBindCol(
    SQLHSTMT StatementHandle,
    SQLUSMALLINT ColumnNumber,
    SQLSMALLINT TargetType,
    SQLPOINTER TargetValuePtr,
    SQLLEN BufferLength,
    SQLLEN *StrLen_or_Ind)
{
    static real_SQLBindCol_t real_fn = NULL;
    if (!real_fn) real_fn = (real_SQLBindCol_t)get_driver_sym("SQLBindCol");
    if (!real_fn) return SQL_ERROR;

    if (TargetType == SQL_C_WCHAR && TargetValuePtr) {
        int slot = -1;
        for (int i = 0; i < conv_col_count; i++) {
            if (conv_cols[i].stmt == StatementHandle && conv_cols[i].col == ColumnNumber) {
                slot = i;
                break;
            }
        }
        if (slot < 0 && conv_col_count < MAX_CONV) slot = conv_col_count++;
        if (slot >= 0) {
            free(conv_cols[slot].driver_target);
            conv_cols[slot].stmt = StatementHandle;
            conv_cols[slot].col = ColumnNumber;
            conv_cols[slot].app_target = TargetValuePtr;
            conv_cols[slot].app_buflen = BufferLength;
            conv_cols[slot].app_ind = StrLen_or_Ind;

            size_t nchars = BufferLength / sizeof(unsigned short);
            conv_cols[slot].driver_buflen = nchars * sizeof(wchar_t);
            conv_cols[slot].driver_target = (wchar_t *)malloc(conv_cols[slot].driver_buflen + sizeof(wchar_t));
            conv_cols[slot].driver_ind = 0;

            return real_fn(StatementHandle, ColumnNumber, SQL_C_WCHAR,
                           conv_cols[slot].driver_target, conv_cols[slot].driver_buflen,
                           &conv_cols[slot].driver_ind);
        }
    }

    return real_fn(StatementHandle, ColumnNumber, TargetType, TargetValuePtr, BufferLength, StrLen_or_Ind);
}

SQLRETURN my_SQLFetch(SQLHSTMT StatementHandle) {
    static real_SQLFetch_t real_fn = NULL;
    if (!real_fn) real_fn = (real_SQLFetch_t)get_driver_sym("SQLFetch");
    if (!real_fn) return SQL_ERROR;

    SQLRETURN rc = real_fn(StatementHandle);
    if (SQL_SUCCEEDED(rc)) {
        for (int i = 0; i < conv_col_count; i++) {
            if (conv_cols[i].stmt == StatementHandle) {
                if (conv_cols[i].driver_ind == SQL_NULL_DATA) {
                    if (conv_cols[i].app_ind) *conv_cols[i].app_ind = SQL_NULL_DATA;
                } else if (conv_cols[i].driver_ind >= 0) {
                    size_t nchars = conv_cols[i].driver_ind / sizeof(wchar_t);
                    size_t max_app_chars = conv_cols[i].app_buflen / sizeof(unsigned short);
                    size_t to_copy = (nchars < max_app_chars - 1) ? nchars : (max_app_chars - 1);
                    unsigned short *dst = (unsigned short *)conv_cols[i].app_target;
                    for (size_t c = 0; c < to_copy; c++) {
                        dst[c] = (unsigned short)conv_cols[i].driver_target[c];
                    }
                    dst[to_copy] = 0;
                    if (conv_cols[i].app_ind) {
                        *conv_cols[i].app_ind = to_copy * sizeof(unsigned short);
                    }
                }
            }
        }
    }
    return rc;
}

void *lt_dlsym(void *handle, const char *symbol) {
    void *(*real_lt_dlsym)(void *, const char *) = dlsym(RTLD_NEXT, "lt_dlsym");
    if (!real_lt_dlsym) return NULL;

    // Only intercept for Informix driver handle
    if (symbol && real_lt_dlsym(handle, "_OdbcRecordError") != NULL) {
        if (strcmp(symbol, "SQLGetDiagRecW") == 0) return (void *)my_SQLGetDiagRecW;
        if (strcmp(symbol, "SQLBindParameter") == 0) return (void *)my_SQLBindParameter;
        if (strcmp(symbol, "SQLBindCol") == 0) return (void *)my_SQLBindCol;
        if (strcmp(symbol, "SQLFetch") == 0) return (void *)my_SQLFetch;
    }
    return real_lt_dlsym(handle, symbol);
}
