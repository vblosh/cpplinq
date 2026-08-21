#include <gtest/gtest.h>
#include "driver/odbc_utils.h"

using namespace cpplinq;
using namespace cpplinq::detail::odbc;

#if defined(CPPLINQ_HAS_MSSQL) || defined(CPPLINQ_HAS_MYSQL) || defined(CPPLINQ_HAS_POSTGRES) || defined(CPPLINQ_HAS_INFORMIX) || defined(CPPLINQ_HAS_ORACLE)

TEST(OdbcUtilsTest, ErrorHandling) {
    std::string err = get_odbc_error(SQL_HANDLE_STMT, SQL_NULL_HANDLE);
    EXPECT_EQ(err, "Unknown ODBC error");

    EXPECT_THROW({
        check_rc(SQL_ERROR, SQL_HANDLE_STMT, SQL_NULL_HANDLE, "TestContext");
    }, DbException);
}

TEST(OdbcUtilsTest, WideStringConversions) {
    std::wstring empty_w = sqlwchar_to_wstring(nullptr, 0);
    EXPECT_TRUE(empty_w.empty());

    std::wstring ws = L"Hello, World!";
    auto sqlw = wstring_to_sqlwchar(ws);
    EXPECT_FALSE(sqlw.empty());

    std::wstring roundtrip = sqlwchar_to_wstring(sqlw.data(), sqlw.size());
    EXPECT_EQ(roundtrip, ws);

    // Surrogate pair test
    std::wstring emoji = L"\U0001F600"; // grinning face
    auto sqlw_emoji = wstring_to_sqlwchar(emoji);
    std::wstring roundtrip_emoji = sqlwchar_to_wstring(sqlw_emoji.data(), sqlw_emoji.size());
    EXPECT_FALSE(roundtrip_emoji.empty());
}

TEST(OdbcUtilsTest, NumericStructConversions) {
    SQL_NUMERIC_STRUCT ns_zero = string_to_numeric_struct("0", 1, 0);
    EXPECT_EQ(numeric_struct_to_string(ns_zero), "0");

    SQL_NUMERIC_STRUCT ns_zero_scaled = string_to_numeric_struct("0.00", 3, 2);
    EXPECT_EQ(numeric_struct_to_string(ns_zero_scaled), "0.00");

    SQL_NUMERIC_STRUCT ns_pos = string_to_numeric_struct("12345.6789", 10, 4);
    EXPECT_EQ(numeric_struct_to_string(ns_pos), "12345.6789");

    SQL_NUMERIC_STRUCT ns_neg = string_to_numeric_struct("-9876.54", 8, 2);
    EXPECT_EQ(numeric_struct_to_string(ns_neg), "-9876.54");

    SQL_NUMERIC_STRUCT ns_small = string_to_numeric_struct("0.005", 5, 3);
    EXPECT_EQ(numeric_struct_to_string(ns_small), "0.005");
}

TEST(OdbcUtilsTest, IntervalStructConversions) {
    std::vector<IntervalType> types = {
        IntervalType::Year,
        IntervalType::Month,
        IntervalType::YearToMonth,
        IntervalType::Day,
        IntervalType::Hour,
        IntervalType::Minute,
        IntervalType::Second,
        IntervalType::DayToHour,
        IntervalType::DayToMinute,
        IntervalType::DayToSecond
    };

    for (auto type : types) {
        SqlInterval iv;
        iv.type = type;
        iv.years = 3;
        iv.months = 5;
        iv.days = 7;
        iv.hours = 9;
        iv.minutes = 11;
        iv.seconds = 13;
        iv.fraction = 123456;
        iv.is_negative = false;

        auto odbc_s = interval_to_odbc_struct(iv);
        auto back_iv = odbc_struct_to_interval(odbc_s);

        EXPECT_EQ(back_iv.type, iv.type);
        EXPECT_EQ(back_iv.is_negative, iv.is_negative);

        // Negative check
        iv.is_negative = true;
        auto odbc_neg = interval_to_odbc_struct(iv);
        auto back_neg = odbc_struct_to_interval(odbc_neg);
        EXPECT_TRUE(back_neg.is_negative);
    }
}

TEST(OdbcUtilsTest, GuidStructConversions) {
    SqlGuid g("12345678-1234-5678-1234-567812345678");
    SQLGUID odbc_g = guid_to_odbc_struct(g);
    SqlGuid back_g = odbc_struct_to_guid(odbc_g);
    EXPECT_EQ(back_g, g);
}

TEST(OdbcUtilsTest, BatchInsertZeroRows) {
    SQLHDBC hdbc = SQL_NULL_HDBC;
    std::vector<BoundValue> params;
    size_t rows = execute_insert_many_batch(hdbc, "INSERT INTO t VALUES (?)", params, 0, 0, "TestDriver");
    EXPECT_EQ(rows, 0);
}

#endif
