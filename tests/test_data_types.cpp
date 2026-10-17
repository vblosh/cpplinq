#include <gtest/gtest.h>
#include "cpplinq/mapping/data_types.h"
#include <limits>
#include <chrono>

using namespace cpplinq;

// 1. Chrono Duration Traits
TEST(DataTypesTest, ChronoDurationTrait) {
    EXPECT_TRUE(is_chrono_duration_v<std::chrono::seconds>);
    EXPECT_TRUE(is_chrono_duration_v<std::chrono::milliseconds>);
    EXPECT_TRUE(is_chrono_duration_v<std::chrono::nanoseconds>);
    EXPECT_FALSE(is_chrono_duration_v<int>);
    EXPECT_FALSE(is_chrono_duration_v<std::string>);
}

// 2. Unicode / UTF-8 / UTF-16 Conversion Helpers
TEST(DataTypesTest, Utf8AndWstringConversions) {
    EXPECT_TRUE(wstring_to_utf8(L"").empty());
    EXPECT_TRUE(utf8_to_wstring("").empty());

    // 1-byte ASCII
    std::wstring ascii_w = L"Hello, World!";
    std::string ascii_u8 = wstring_to_utf8(ascii_w);
    EXPECT_EQ(ascii_u8, "Hello, World!");
    EXPECT_EQ(utf8_to_wstring(ascii_u8), ascii_w);

    // 2-byte UTF-8 (Greek / Cyrillic)
    std::wstring greek_w = L"Ελληνικά";
    std::string greek_u8 = wstring_to_utf8(greek_w);
    EXPECT_EQ(utf8_to_wstring(greek_u8), greek_w);

    // 3-byte UTF-8 (CJK / Symbols)
    std::wstring cjk_w = L"日本語と中文";
    std::string cjk_u8 = wstring_to_utf8(cjk_w);
    EXPECT_EQ(utf8_to_wstring(cjk_u8), cjk_w);

    // 4-byte UTF-8 (Emojis)
    std::wstring emoji_w = L"🚀🌟🔥";
    std::string emoji_u8 = wstring_to_utf8(emoji_w);
    EXPECT_EQ(utf8_to_wstring(emoji_u8), emoji_w);
}

// 3. SqlGuid
TEST(DataTypesTest, SqlGuidComprehensive) {
    // Default constructor & is_nil
    SqlGuid g0;
    EXPECT_TRUE(g0.is_nil());
    EXPECT_EQ(g0.to_string(), "00000000-0000-0000-0000-000000000000");

    // Array constructor
    std::array<uint8_t, 16> arr = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
    SqlGuid g_arr(arr);
    EXPECT_FALSE(g_arr.is_nil());
    EXPECT_EQ(g_arr.bytes, arr);

    // Raw pointer constructor
    SqlGuid g_ptr(arr.data());
    EXPECT_EQ(g_ptr, g_arr);
    SqlGuid g_null_ptr(static_cast<const uint8_t*>(nullptr));
    EXPECT_TRUE(g_null_ptr.is_nil());

    // Structured fields constructor (Data1, Data2, Data3, Data4)
    uint8_t d4[8] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    SqlGuid g_struct(0xA1B2C3D4, 0xE5F6, 0x7A8B, d4);
    EXPECT_EQ(g_struct.bytes[0], 0xA1);
    EXPECT_EQ(g_struct.bytes[1], 0xB2);
    EXPECT_EQ(g_struct.bytes[2], 0xC3);
    EXPECT_EQ(g_struct.bytes[3], 0xD4);
    EXPECT_EQ(g_struct.bytes[4], 0xE5);
    EXPECT_EQ(g_struct.bytes[5], 0xF6);
    EXPECT_EQ(g_struct.bytes[6], 0x7A);
    EXPECT_EQ(g_struct.bytes[7], 0x8B);
    EXPECT_EQ(g_struct.bytes[8], 0x11);

    // Formatted output options (with/without hyphens, upper/lower)
    EXPECT_EQ(g_struct.to_string(true, false), "a1b2c3d4-e5f6-7a8b-1122-334455667788");
    EXPECT_EQ(g_struct.to_string(false, false), "a1b2c3d4e5f67a8b1122334455667788");
    EXPECT_EQ(g_struct.to_string(true, true), "A1B2C3D4-E5F6-7A8B-1122-334455667788");
    EXPECT_EQ(g_struct.to_string(false, true), "A1B2C3D4E5F67A8B1122334455667788");

    // String parsing
    SqlGuid parsed = SqlGuid::from_string("A1B2C3D4-E5F6-7A8B-1122-334455667788");
    EXPECT_EQ(parsed, g_struct);
    EXPECT_EQ(SqlGuid::from_string(""), g0);
    EXPECT_EQ(SqlGuid::from_string("invalid_length_string"), g0);

    // Random new_guid()
    SqlGuid random_guid1 = SqlGuid::new_guid();
    SqlGuid random_guid2 = SqlGuid::new_guid();
    EXPECT_FALSE(random_guid1.is_nil());
    EXPECT_FALSE(random_guid2.is_nil());
    EXPECT_NE(random_guid1, random_guid2);
    // RFC 4122 v4 variant and version checks
    EXPECT_EQ(random_guid1.bytes[6] & 0xF0, 0x40); // version 4
    EXPECT_EQ(random_guid1.bytes[8] & 0xC0, 0x80); // variant 1

    // Comparisons
    SqlGuid smaller("00000000-0000-0000-0000-000000000001");
    SqlGuid larger("ffffffff-ffff-ffff-ffff-ffffffffffff");
    EXPECT_TRUE(smaller < larger);
    EXPECT_TRUE(smaller <= larger);
    EXPECT_FALSE(smaller > larger);
    EXPECT_FALSE(smaller >= larger);
}

// 4. SqlNumeric
TEST(DataTypesTest, SqlNumericComprehensive) {
    // Default & Null
    SqlNumeric n_def;
    EXPECT_EQ(n_def.to_string(), "0");
    EXPECT_EQ(n_def.to_int64(), 0);
    EXPECT_EQ(n_def.to_uint64(), 0ULL);
    EXPECT_DOUBLE_EQ(n_def.to_double(), 0.0);

    // String constructor & whitespace trimming
    SqlNumeric n_trim("   \t  +123.4500  \n ");
    EXPECT_EQ(n_trim.to_string(), "123.4500");
    EXPECT_EQ(n_trim.to_int64(), 123);
    EXPECT_EQ(n_trim.to_uint64(), 123ULL);
    EXPECT_NEAR(n_trim.to_double(), 123.45, 1e-4);

    SqlNumeric n_neg_str("  -999.888  ");
    EXPECT_EQ(n_neg_str.to_string(), "-999.888");
    EXPECT_EQ(n_neg_str.to_int64(), -999);
    EXPECT_EQ(n_neg_str.to_uint64(), 0ULL); // Negative uint64 returns 0
    EXPECT_NEAR(n_neg_str.to_double(), -999.888, 1e-4);

    SqlNumeric n_empty_str("   ");
    EXPECT_EQ(n_empty_str.to_string(), "0");

    // Integer constructors
    SqlNumeric n_i32(int32_t(-42));
    EXPECT_EQ(n_i32.to_int64(), -42);
    EXPECT_EQ(n_i32.to_string(), "-42");

    SqlNumeric n_u32(uint32_t(500));
    EXPECT_EQ(n_u32.to_int64(), 500);
    EXPECT_EQ(n_u32.to_uint64(), 500ULL);

    SqlNumeric n_i64_pos(int64_t(9876543210LL));
    EXPECT_EQ(n_i64_pos.to_int64(), 9876543210LL);
    EXPECT_EQ(n_i64_pos.to_uint64(), 9876543210ULL);

    SqlNumeric n_i64_neg(int64_t(-9876543210LL));
    EXPECT_EQ(n_i64_neg.to_int64(), -9876543210LL);
    EXPECT_EQ(n_i64_neg.to_string(), "-9876543210");

    SqlNumeric n_u64(uint64_t(18446744073709551615ULL));
    EXPECT_EQ(n_u64.to_string(), "18446744073709551615");

    // Double constructor (including NaN)
    SqlNumeric n_nan(std::numeric_limits<double>::quiet_NaN());
    EXPECT_EQ(n_nan.to_string(), "0");

    SqlNumeric n_dbl(3.14159265, 4);
    EXPECT_EQ(n_dbl.to_string(), "3.1416");

    // const char* and std::string ctors
    const char* c_str = "456.78";
    SqlNumeric n_from_cstr(c_str);
    EXPECT_EQ(n_from_cstr.to_string(), "456.78");
    const char* null_cstr = nullptr;
    SqlNumeric n_from_null_cstr(null_cstr);
    EXPECT_EQ(n_from_null_cstr.to_string(), "0");

    // Comparisons
    SqlNumeric small_val("10.0");
    SqlNumeric large_val("20.0");
    EXPECT_TRUE(small_val < large_val);
    EXPECT_TRUE(small_val <= large_val);
    EXPECT_FALSE(small_val > large_val);
    EXPECT_FALSE(small_val >= large_val);
    EXPECT_TRUE(small_val == SqlNumeric("10.0"));
    EXPECT_TRUE(small_val != large_val);
}

// 5. SqlDate
TEST(DataTypesTest, SqlDateComprehensive) {
    SqlDate d0;
    EXPECT_EQ(d0.to_string(), "1970-01-01");

    SqlDate d(2026, 8, 21);
    EXPECT_EQ(d.to_string(), "2026-08-21");

    SqlDate parsed = SqlDate::from_string("2030-12-31");
    EXPECT_EQ(parsed.year, 2030);
    EXPECT_EQ(parsed.month, 12);
    EXPECT_EQ(parsed.day, 31);
    EXPECT_EQ(parsed.to_string(), "2030-12-31");

    EXPECT_EQ(SqlDate::from_string("").year, 1970);

    SqlDate d_copy("2026-08-21");
    EXPECT_EQ(d, d_copy);
    EXPECT_TRUE(d < parsed);
}

// 6. SqlTime
TEST(DataTypesTest, SqlTimeComprehensive) {
    SqlTime t0;
    EXPECT_EQ(t0.to_string(false), "00:00:00");
    EXPECT_EQ(t0.to_string(true), "00:00:00.000000");

    SqlTime t_no_frac(15, 30, 45);
    EXPECT_EQ(t_no_frac.to_string(false), "15:30:45");
    EXPECT_EQ(t_no_frac.to_string(true), "15:30:45.000000");

    SqlTime t1(15, 30, 45, 123456000);
    EXPECT_EQ(t1.to_string(false), "15:30:45.123456");
    EXPECT_EQ(t1.to_string(true), "15:30:45.123456");

    SqlTime parsed_no_frac = SqlTime::from_string("08:15:30");
    EXPECT_EQ(parsed_no_frac.hour, 8);
    EXPECT_EQ(parsed_no_frac.minute, 15);
    EXPECT_EQ(parsed_no_frac.second, 30);
    EXPECT_EQ(parsed_no_frac.fraction, 0);

    SqlTime parsed_frac = SqlTime::from_string("08:15:30.987654321");
    EXPECT_EQ(parsed_frac.hour, 8);
    EXPECT_EQ(parsed_frac.minute, 15);
    EXPECT_EQ(parsed_frac.second, 30);
    EXPECT_EQ(parsed_frac.fraction, 987654321);

    EXPECT_EQ(SqlTime::from_string("").hour, 0);

    EXPECT_TRUE(parsed_no_frac < t1);
    EXPECT_EQ(SqlTime("15:30:45"), SqlTime(15, 30, 45));
}

// 7. SqlTimestamp
TEST(DataTypesTest, SqlTimestampComprehensive) {
    SqlTimestamp ts0;
    EXPECT_EQ(ts0.to_string(false), "1970-01-01 00:00:00");

    SqlTimestamp ts_no_frac(2026, 8, 21, 15, 45, 30);
    EXPECT_EQ(ts_no_frac.to_string(false), "2026-08-21 15:45:30");
    EXPECT_EQ(ts_no_frac.to_string(true), "2026-08-21 15:45:30.000000");

    SqlTimestamp ts(2026, 8, 21, 15, 45, 30, 500000000);
    EXPECT_EQ(ts.to_string(false), "2026-08-21 15:45:30.500000");
    EXPECT_EQ(ts.to_string(true), "2026-08-21 15:45:30.500000");

    SqlTimestamp parsed_iso = SqlTimestamp::from_string("2026-08-21T15:45:30.123456789");
    EXPECT_EQ(parsed_iso.year, 2026);
    EXPECT_EQ(parsed_iso.month, 8);
    EXPECT_EQ(parsed_iso.day, 21);
    EXPECT_EQ(parsed_iso.hour, 15);
    EXPECT_EQ(parsed_iso.minute, 45);
    EXPECT_EQ(parsed_iso.second, 30);
    EXPECT_EQ(parsed_iso.fraction, 123456789);

    EXPECT_EQ(SqlTimestamp::from_string("").year, 1970);

    // system_clock::time_point roundtrip
    auto now = std::chrono::system_clock::now();
    SqlTimestamp from_now(now);
    auto back_now = from_now.to_time_point();
    auto diff = std::chrono::duration_cast<std::chrono::seconds>(now - back_now).count();
    EXPECT_LE(std::abs(diff), 1);

    SqlTimestamp ts_str_ctor("2026-08-21 15:45:30");
    EXPECT_EQ(ts_str_ctor.year, 2026);
    EXPECT_EQ(ts_str_ctor.second, 30);
}

// 8. SqlInterval
TEST(DataTypesTest, SqlIntervalComprehensive) {
    // Default
    SqlInterval iv0;
    EXPECT_EQ(iv0.type, IntervalType::DayToSecond);
    EXPECT_FALSE(iv0.is_negative);

    // Factory methods
    auto iv_ym = SqlInterval::from_year_month(5, 6, false);
    EXPECT_EQ(iv_ym.type, IntervalType::YearToMonth);
    EXPECT_EQ(iv_ym.years, 5);
    EXPECT_EQ(iv_ym.months, 6);
    EXPECT_EQ(iv_ym.to_string(), "5-6");

    auto iv_ym_neg = SqlInterval::from_year_month(2, 3, true);
    EXPECT_EQ(iv_ym_neg.to_string(), "-2-3");

    auto iv_ds = SqlInterval::from_day_second(10, 4, 30, 25, 500000000, false);
    EXPECT_EQ(iv_ds.days, 10);
    EXPECT_EQ(iv_ds.hours, 4);
    EXPECT_EQ(iv_ds.minutes, 30);
    EXPECT_EQ(iv_ds.seconds, 25);
    EXPECT_EQ(iv_ds.fraction, 500000000);
    EXPECT_EQ(iv_ds.to_string(), "10 04:30:25.500000");

    auto iv_ds_neg = SqlInterval::from_day_second(1, 2, 3, 4, 0, true);
    EXPECT_EQ(iv_ds_neg.to_string(), "-1 02:03:04");

    // Chrono duration roundtrip
    auto dur = std::chrono::hours(50) + std::chrono::minutes(20) + std::chrono::seconds(15);
    auto iv_dur = SqlInterval::from_duration(dur);
    EXPECT_EQ(iv_dur.days, 2);
    EXPECT_EQ(iv_dur.hours, 2);
    EXPECT_EQ(iv_dur.minutes, 20);
    EXPECT_EQ(iv_dur.seconds, 15);

    auto back_dur = iv_dur.to_duration<std::chrono::seconds>();
    EXPECT_EQ(back_dur, std::chrono::duration_cast<std::chrono::seconds>(dur));

    // String parsing
    auto parsed_ym = SqlInterval::from_string("10-11");
    EXPECT_EQ(parsed_ym.type, IntervalType::YearToMonth);
    EXPECT_EQ(parsed_ym.years, 10);
    EXPECT_EQ(parsed_ym.months, 11);

    auto parsed_days_word = SqlInterval::from_string("5 days 04:15:30");
    EXPECT_EQ(parsed_days_word.days, 5);
    EXPECT_EQ(parsed_days_word.hours, 4);
    EXPECT_EQ(parsed_days_word.minutes, 15);
    EXPECT_EQ(parsed_days_word.seconds, 30);

    auto parsed_space = SqlInterval::from_string("3 12:00:00");
    EXPECT_EQ(parsed_space.days, 3);
    EXPECT_EQ(parsed_space.hours, 12);

    auto parsed_colon_only = SqlInterval::from_string("06:30:45");
    EXPECT_EQ(parsed_colon_only.hours, 6);
    EXPECT_EQ(parsed_colon_only.minutes, 30);
    EXPECT_EQ(parsed_colon_only.seconds, 45);

    auto parsed_neg = SqlInterval::from_string("-2 05:10:20");
    EXPECT_TRUE(parsed_neg.is_negative);
    EXPECT_EQ(parsed_neg.days, 2);

    EXPECT_TRUE(SqlInterval::from_string("").days == 0);
}
