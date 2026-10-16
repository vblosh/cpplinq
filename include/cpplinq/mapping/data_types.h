#pragma once
#include <string>
#include <string_view>
#include <cstdint>
#include <chrono>
#include <compare>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <vector>
#include <array>
#include <variant>
#include <random>
#include <cctype>
#include <cmath>
#include <ctime>
#include <cstring>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace cpplinq {

// ----------------------------------------------------------------------------
// Chrono duration trait
// ----------------------------------------------------------------------------
template <typename T>
struct is_chrono_duration : std::false_type {};

template <typename Rep, typename Period>
struct is_chrono_duration<std::chrono::duration<Rep, Period>> : std::true_type {};

template <typename T>
inline constexpr bool is_chrono_duration_v = is_chrono_duration<T>::value;

template <typename T>
struct is_hh_mm_ss : std::false_type {};

template <typename Duration>
struct is_hh_mm_ss<std::chrono::hh_mm_ss<Duration>> : std::true_type {};

template <typename T>
inline constexpr bool is_hh_mm_ss_v = is_hh_mm_ss<T>::value;

// ----------------------------------------------------------------------------
// Unicode / UTF-8 / UTF-16 helpers
// ----------------------------------------------------------------------------

inline std::string wstring_to_utf8(std::wstring_view wstr) {
    if (wstr.empty()) return {};
#if defined(_WIN32)
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string str(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), str.data(), len, nullptr, nullptr);
    return str;
#else
    std::string str;
    str.reserve(wstr.size() * 2);
    for (wchar_t wc : wstr) {
        uint32_t cp = static_cast<uint32_t>(wc);
        if (cp < 0x80) {
            str.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            str.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
            str.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            str.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
            str.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            str.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            str.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
            str.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            str.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            str.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return str;
#endif
}

inline std::wstring utf8_to_wstring(std::string_view str) {
    if (str.empty()) return {};
#if defined(_WIN32)
    int len = MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), nullptr, 0);
    if (len <= 0) return {};
    std::wstring wstr(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), wstr.data(), len);
    return wstr;
#else
    std::wstring wstr;
    wstr.reserve(str.size());
    for (size_t i = 0; i < str.size();) {
        unsigned char c = static_cast<unsigned char>(str[i]);
        if (c < 0x80) {
            wstr.push_back(static_cast<wchar_t>(c));
            i += 1;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < str.size()) {
            wchar_t wc = ((c & 0x1F) << 6) | (static_cast<unsigned char>(str[i + 1]) & 0x3F);
            wstr.push_back(wc);
            i += 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < str.size()) {
            wchar_t wc = ((c & 0x0F) << 12) | ((static_cast<unsigned char>(str[i + 1]) & 0x3F) << 6) | (static_cast<unsigned char>(str[i + 2]) & 0x3F);
            wstr.push_back(wc);
            i += 3;
        } else if ((c & 0xF8) == 0xF0 && i + 3 < str.size()) {
            wchar_t wc = ((c & 0x07) << 18) | ((static_cast<unsigned char>(str[i + 1]) & 0x3F) << 12) | ((static_cast<unsigned char>(str[i + 2]) & 0x3F) << 6) | (static_cast<unsigned char>(str[i + 3]) & 0x3F);
            wstr.push_back(wc);
            i += 4;
        } else {
            wstr.push_back(static_cast<wchar_t>(c));
            i += 1;
        }
    }
    return wstr;
#endif
}

// ----------------------------------------------------------------------------
// SqlGuid: 128-bit UUID / GUID
// ----------------------------------------------------------------------------
struct SqlGuid {
    std::array<uint8_t, 16> bytes{};

    SqlGuid() = default;

    explicit SqlGuid(const std::array<uint8_t, 16>& b) : bytes(b) {}
    explicit SqlGuid(const uint8_t* b) {
        if (b) std::memcpy(bytes.data(), b, 16);
    }

    explicit SqlGuid(std::string_view str) {
        *this = from_string(str);
    }

    SqlGuid(uint32_t data1, uint16_t data2, uint16_t data3, const uint8_t data4[8]) {
        bytes[0] = static_cast<uint8_t>((data1 >> 24) & 0xFF);
        bytes[1] = static_cast<uint8_t>((data1 >> 16) & 0xFF);
        bytes[2] = static_cast<uint8_t>((data1 >> 8) & 0xFF);
        bytes[3] = static_cast<uint8_t>(data1 & 0xFF);
        bytes[4] = static_cast<uint8_t>((data2 >> 8) & 0xFF);
        bytes[5] = static_cast<uint8_t>(data2 & 0xFF);
        bytes[6] = static_cast<uint8_t>((data3 >> 8) & 0xFF);
        bytes[7] = static_cast<uint8_t>(data3 & 0xFF);
        if (data4) {
            std::memcpy(&bytes[8], data4, 8);
        }
    }

    bool is_nil() const {
        for (uint8_t b : bytes) {
            if (b != 0) return false;
        }
        return true;
    }

    std::string to_string(bool with_hyphens = true, bool upper_case = false) const {
        static const char* hex_lower = "0123456789abcdef";
        static const char* hex_upper = "0123456789ABCDEF";
        const char* hex = upper_case ? hex_upper : hex_lower;

        std::string s;
        s.reserve(with_hyphens ? 36 : 32);

        for (size_t i = 0; i < 16; ++i) {
            if (with_hyphens && (i == 4 || i == 6 || i == 8 || i == 10)) {
                s.push_back('-');
            }
            s.push_back(hex[(bytes[i] >> 4) & 0x0F]);
            s.push_back(hex[bytes[i] & 0x0F]);
        }
        return s;
    }

    static SqlGuid from_string(std::string_view str) {
        SqlGuid g;
        if (str.empty()) return g;

        std::string hex_only;
        hex_only.reserve(32);
        for (char c : str) {
            if (std::isxdigit(static_cast<unsigned char>(c))) {
                hex_only.push_back(c);
            }
        }

        if (hex_only.size() != 32) return g;

        auto hex_val = [](char c) -> uint8_t {
            if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
            if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(c - 'a' + 10);
            if (c >= 'A' && c <= 'F') return static_cast<uint8_t>(c - 'A' + 10);
            return 0;
        };

        for (size_t i = 0; i < 16; ++i) {
            g.bytes[i] = static_cast<uint8_t>((hex_val(hex_only[i * 2]) << 4) | hex_val(hex_only[i * 2 + 1]));
        }
        return g;
    }

    static SqlGuid new_guid() {
        SqlGuid g;
        static thread_local std::mt19937_64 rng(std::random_device{}());
        uint64_t r1 = rng();
        uint64_t r2 = rng();
        std::memcpy(&g.bytes[0], &r1, 8);
        std::memcpy(&g.bytes[8], &r2, 8);
        g.bytes[6] = static_cast<uint8_t>((g.bytes[6] & 0x0F) | 0x40);
        g.bytes[8] = static_cast<uint8_t>((g.bytes[8] & 0x3F) | 0x80);
        return g;
    }

    bool operator==(const SqlGuid& other) const = default;
    auto operator<=>(const SqlGuid& other) const = default;
};

// ----------------------------------------------------------------------------
// SqlNumeric: Exact arbitrary-precision decimal
// ----------------------------------------------------------------------------
struct SqlNumeric {
    std::string value = "0";
    uint8_t precision = 0;
    int8_t scale = 0;
    bool is_negative = false;

    SqlNumeric() = default;
    
    SqlNumeric(std::string_view str) {
        assign_string(str);
    }
    
    SqlNumeric(const char* str) : SqlNumeric(std::string_view(str ? str : "0")) {}
    SqlNumeric(const std::string& str) : SqlNumeric(std::string_view(str)) {}
    
    SqlNumeric(int64_t val) {
        is_negative = (val < 0);
        uint64_t mag = is_negative
            ? (0ULL - static_cast<uint64_t>(val))
            : static_cast<uint64_t>(val);
        value = (is_negative ? "-" : "") + std::to_string(mag);
        scale = 0;
        precision = static_cast<uint8_t>(value.size() - (is_negative ? 1 : 0));
    }
    
    SqlNumeric(uint64_t val) {
        is_negative = false;
        value = std::to_string(val);
        scale = 0;
        precision = static_cast<uint8_t>(value.size());
    }

    SqlNumeric(int val) : SqlNumeric(static_cast<int64_t>(val)) {}
    SqlNumeric(uint32_t val) : SqlNumeric(static_cast<uint64_t>(val)) {}

    SqlNumeric(double val, int target_scale = 6) {
        if (std::isnan(val) || std::isinf(val)) {
            value = "0";
            precision = 1;
            scale = 0;
            is_negative = false;
            return;
        }
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(target_scale) << val;
        assign_string(oss.str());
    }

    std::string to_string() const {
        return value.empty() ? "0" : value;
    }

    double to_double() const {
        try {
            return std::stod(value);
        } catch (...) {
            return 0.0;
        }
    }

    int64_t to_int64() const {
        try {
            size_t dot = value.find('.');
            std::string int_part = (dot == std::string::npos) ? value : value.substr(0, dot);
            return std::stoll(int_part);
        } catch (...) {
            return 0;
        }
    }

    uint64_t to_uint64() const {
        try {
            size_t dot = value.find('.');
            std::string int_part = (dot == std::string::npos) ? value : value.substr(0, dot);
            if (!int_part.empty() && int_part.front() == '-') return 0;
            return std::stoull(int_part);
        } catch (...) {
            return 0;
        }
    }

    std::strong_ordering operator<=>(const SqlNumeric& other) const {
        bool this_zero = is_zero();
        bool other_zero = other.is_zero();
        if (this_zero && other_zero) {
            return std::strong_ordering::equal;
        }
        if (this_zero) {
            return other.is_negative ? std::strong_ordering::greater : std::strong_ordering::less;
        }
        if (other_zero) {
            return is_negative ? std::strong_ordering::less : std::strong_ordering::greater;
        }

        if (is_negative != other.is_negative) {
            return is_negative ? std::strong_ordering::less : std::strong_ordering::greater;
        }

        auto cmp = compare_magnitudes(other);
        if (is_negative) {
            if (cmp == std::strong_ordering::less) return std::strong_ordering::greater;
            if (cmp == std::strong_ordering::greater) return std::strong_ordering::less;
            return std::strong_ordering::equal;
        }
        return cmp;
    }

    bool operator==(const SqlNumeric& other) const {
        return (*this <=> other) == 0;
    }

private:
    bool is_zero() const {
        for (char c : value) {
            if (std::isdigit(static_cast<unsigned char>(c)) && c != '0') {
                return false;
            }
        }
        return true;
    }

    std::strong_ordering compare_magnitudes(const SqlNumeric& other) const {
        std::string_view v1 = value;
        if (!v1.empty() && (v1.front() == '-' || v1.front() == '+')) v1.remove_prefix(1);
        std::string_view v2 = other.value;
        if (!v2.empty() && (v2.front() == '-' || v2.front() == '+')) v2.remove_prefix(1);

        size_t dot1 = v1.find('.');
        std::string_view int1 = (dot1 == std::string_view::npos) ? v1 : v1.substr(0, dot1);
        std::string_view frac1 = (dot1 == std::string_view::npos) ? std::string_view{} : v1.substr(dot1 + 1);

        size_t dot2 = v2.find('.');
        std::string_view int2 = (dot2 == std::string_view::npos) ? v2 : v2.substr(0, dot2);
        std::string_view frac2 = (dot2 == std::string_view::npos) ? std::string_view{} : v2.substr(dot2 + 1);

        while (int1.size() > 1 && int1.front() == '0') int1.remove_prefix(1);
        while (int2.size() > 1 && int2.front() == '0') int2.remove_prefix(1);
        if (int1.empty()) int1 = "0";
        if (int2.empty()) int2 = "0";

        if (int1.size() != int2.size()) {
            return int1.size() <=> int2.size();
        }
        if (int1 != int2) {
            return int1 <=> int2;
        }

        while (!frac1.empty() && frac1.back() == '0') frac1.remove_suffix(1);
        while (!frac2.empty() && frac2.back() == '0') frac2.remove_suffix(1);

        size_t common_len = std::min(frac1.size(), frac2.size());
        for (size_t i = 0; i < common_len; ++i) {
            if (frac1[i] != frac2[i]) {
                return frac1[i] <=> frac2[i];
            }
        }
        return frac1.size() <=> frac2.size();
    }

    void assign_string(std::string_view str) {
        std::string s(str);
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
        if (s.empty()) {
            value = "0";
            precision = 1;
            scale = 0;
            is_negative = false;
            return;
        }
        if (s.front() == '-') {
            is_negative = true;
        } else {
            is_negative = false;
            if (s.front() == '+') s.erase(s.begin());
        }

        bool has_digit = false;
        for (char c : s) {
            if (std::isdigit(static_cast<unsigned char>(c))) {
                has_digit = true;
                break;
            }
        }
        if (!has_digit) {
            value = "0";
            precision = 1;
            scale = 0;
            is_negative = false;
            return;
        }

        value = s;
        size_t dot = s.find('.');
        if (dot != std::string::npos) {
            scale = static_cast<int8_t>(s.size() - dot - 1);
            size_t digits_count = (s.size() > 1 + (is_negative ? 1 : 0)) ? (s.size() - 1 - (is_negative ? 1 : 0)) : 0;
            precision = static_cast<uint8_t>(digits_count);
        } else {
            scale = 0;
            size_t digits_count = (s.size() >= (is_negative ? 1 : 0)) ? (s.size() - (is_negative ? 1 : 0)) : 0;
            precision = static_cast<uint8_t>(digits_count);
        }

        bool all_zero = true;
        for (char c : s) {
            if (std::isdigit(static_cast<unsigned char>(c)) && c != '0') {
                all_zero = false;
                break;
            }
        }
        if (all_zero) {
            is_negative = false;
            if (!value.empty() && value.front() == '-') {
                value.erase(value.begin());
            }
        }
    }
};

// ----------------------------------------------------------------------------
// SqlDate: Year, Month, Day
// ----------------------------------------------------------------------------
struct SqlDate {
    int16_t year = 1970;
    uint8_t month = 1;
    uint8_t day = 1;

    SqlDate() = default;
    SqlDate(int16_t y, uint8_t m, uint8_t d) : year(y), month(m), day(d) {}

    explicit SqlDate(std::chrono::year_month_day ymd)
        : year(static_cast<int16_t>(static_cast<int>(ymd.year()))),
          month(static_cast<uint8_t>(static_cast<unsigned>(ymd.month()))),
          day(static_cast<uint8_t>(static_cast<unsigned>(ymd.day()))) {}

    explicit SqlDate(std::string_view iso_str) {
        *this = from_string(iso_str);
    }

    std::chrono::year_month_day to_year_month_day() const {
        return std::chrono::year_month_day{
            std::chrono::year{year},
            std::chrono::month{month},
            std::chrono::day{day}
        };
    }

    std::string to_string() const {
        std::ostringstream oss;
        oss << std::setfill('0')
            << std::setw(4) << year << "-"
            << std::setw(2) << static_cast<int>(month) << "-"
            << std::setw(2) << static_cast<int>(day);
        return oss.str();
    }

    static SqlDate from_string(std::string_view str) {
        SqlDate d;
        if (str.empty()) return d;
        int y = 0, m = 0, day_val = 0;
        char sep1 = 0, sep2 = 0;
        std::string s_str(str);
        std::istringstream iss(s_str);
        if (iss >> y >> sep1 >> m >> sep2 >> day_val) {
            d.year = static_cast<int16_t>(y);
            d.month = static_cast<uint8_t>(m);
            d.day = static_cast<uint8_t>(day_val);
        }
        return d;
    }

    bool operator==(const SqlDate& other) const = default;
    auto operator<=>(const SqlDate& other) const = default;
};

// ----------------------------------------------------------------------------
// SqlTime: Hour, Minute, Second, Fraction
// ----------------------------------------------------------------------------
struct SqlTime {
    uint8_t hour = 0;
    uint8_t minute = 0;
    uint8_t second = 0;
    uint32_t fraction = 0; // nanoseconds (0..999,999,999)

    SqlTime() = default;
    SqlTime(uint8_t h, uint8_t m, uint8_t s, uint32_t f = 0)
        : hour(h), minute(m), second(s), fraction(f) {}

    template <typename Duration>
    explicit SqlTime(const std::chrono::hh_mm_ss<Duration>& hms)
        : hour(static_cast<uint8_t>(hms.hours().count())),
          minute(static_cast<uint8_t>(hms.minutes().count())),
          second(static_cast<uint8_t>(hms.seconds().count())),
          fraction(static_cast<uint32_t>(std::max<int64_t>(0, std::chrono::duration_cast<std::chrono::nanoseconds>(hms.subseconds()).count()))) {}

    explicit SqlTime(std::string_view str) {
        *this = from_string(str);
    }

    template <typename Duration = std::chrono::nanoseconds>
    std::chrono::hh_mm_ss<Duration> to_hh_mm_ss() const {
        auto total_dur = std::chrono::hours(hour) +
                         std::chrono::minutes(minute) +
                         std::chrono::seconds(second) +
                         std::chrono::nanoseconds(fraction);
        return std::chrono::hh_mm_ss<Duration>(std::chrono::duration_cast<Duration>(total_dur));
    }

    std::string to_string(bool include_fraction = false) const {
        std::ostringstream oss;
        oss << std::setfill('0')
            << std::setw(2) << static_cast<int>(hour) << ":"
            << std::setw(2) << static_cast<int>(minute) << ":"
            << std::setw(2) << static_cast<int>(second);
        if (include_fraction || fraction > 0) {
            oss << "." << std::setfill('0') << std::setw(9) << fraction;
        }
        return oss.str();
    }

    static SqlTime from_string(std::string_view str) {
        SqlTime t;
        if (str.empty()) return t;
        int h = 0, m = 0, s = 0;
        char sep1 = 0, sep2 = 0;
        std::string s_str(str);
        std::istringstream iss(s_str);
        if (iss >> h >> sep1 >> m >> sep2 >> s) {
            t.hour = static_cast<uint8_t>(h);
            t.minute = static_cast<uint8_t>(m);
            t.second = static_cast<uint8_t>(s);
            if (iss.peek() == '.') {
                iss.get();
                std::string frac_str;
                while (std::isdigit(static_cast<unsigned char>(iss.peek()))) {
                    frac_str.push_back(static_cast<char>(iss.get()));
                }
                if (!frac_str.empty()) {
                    if (frac_str.size() > 9) {
                        frac_str.resize(9);
                    } else {
                        while (frac_str.size() < 9) frac_str.push_back('0');
                    }
                    try { t.fraction = static_cast<uint32_t>(std::stoul(frac_str)); } catch (...) {}
                }
            }
        }
        return t;
    }

    bool operator==(const SqlTime& other) const = default;
    auto operator<=>(const SqlTime& other) const = default;
};

// ----------------------------------------------------------------------------
// SqlTimestamp: Year, Month, Day, Hour, Minute, Second, Fraction, TZ
// ----------------------------------------------------------------------------
struct SqlTimestamp {
    int16_t year = 1970;
    uint8_t month = 1;
    uint8_t day = 1;
    uint8_t hour = 0;
    uint8_t minute = 0;
    uint8_t second = 0;
    uint32_t fraction = 0; // nanoseconds
    int16_t tz_offset_minutes = 0;

    SqlTimestamp() = default;
    SqlTimestamp(int16_t y, uint8_t mo, uint8_t d, uint8_t h = 0, uint8_t mi = 0, uint8_t s = 0, uint32_t frac = 0, int16_t tz = 0)
        : year(y), month(mo), day(d), hour(h), minute(mi), second(s), fraction(frac), tz_offset_minutes(tz) {}

    explicit SqlTimestamp(std::string_view iso_str) {
        *this = from_string(iso_str);
    }

    explicit SqlTimestamp(std::chrono::system_clock::time_point tp) {
        *this = from_time_point(tp);
    }

    std::string to_string(bool include_fraction = false) const {
        std::ostringstream oss;
        oss << std::setfill('0')
            << std::setw(4) << year << "-"
            << std::setw(2) << static_cast<int>(month) << "-"
            << std::setw(2) << static_cast<int>(day) << " "
            << std::setw(2) << static_cast<int>(hour) << ":"
            << std::setw(2) << static_cast<int>(minute) << ":"
            << std::setw(2) << static_cast<int>(second);
        if (include_fraction || fraction > 0) {
            oss << "." << std::setfill('0') << std::setw(9) << fraction;
        }
        if (tz_offset_minutes != 0) {
            int h = std::abs(tz_offset_minutes) / 60;
            int m = std::abs(tz_offset_minutes) % 60;
            oss << (tz_offset_minutes < 0 ? "-" : "+")
                << std::setfill('0') << std::setw(2) << h << ":"
                << std::setfill('0') << std::setw(2) << m;
        }
        return oss.str();
    }

    static SqlTimestamp from_string(std::string_view str) {
        SqlTimestamp ts;
        if (str.empty()) return ts;
        std::string s(str);
        for (char& c : s) {
            if (c == 'T') c = ' ';
        }
        int y = 0, mo = 0, d = 0, h = 0, mi = 0, sec = 0;
        char s1 = 0, s2 = 0, s3 = 0, s4 = 0;
        std::istringstream iss(s);
        if (iss >> y >> s1 >> mo >> s2 >> d) {
            ts.year = static_cast<int16_t>(y);
            ts.month = static_cast<uint8_t>(mo);
            ts.day = static_cast<uint8_t>(d);
            if (iss >> h >> s3 >> mi >> s4 >> sec) {
                ts.hour = static_cast<uint8_t>(h);
                ts.minute = static_cast<uint8_t>(mi);
                ts.second = static_cast<uint8_t>(sec);
                if (iss.peek() == '.') {
                    iss.get();
                    std::string frac_str;
                    while (std::isdigit(static_cast<unsigned char>(iss.peek()))) {
                        frac_str.push_back(static_cast<char>(iss.get()));
                    }
                    if (!frac_str.empty()) {
                        if (frac_str.size() > 9) {
                            frac_str.resize(9);
                        } else {
                            while (frac_str.size() < 9) frac_str.push_back('0');
                        }
                        try { ts.fraction = static_cast<uint32_t>(std::stoul(frac_str)); } catch (...) {}
                    }
                }
                while (std::isspace(static_cast<unsigned char>(iss.peek()))) {
                    iss.get();
                }
                int c = iss.peek();
                if (c == 'Z' || c == 'z') {
                    iss.get();
                    ts.tz_offset_minutes = 0;
                } else if (c == '+' || c == '-') {
                    char sign = static_cast<char>(iss.get());
                    int tz_h = 0;
                    char tz_sep = 0;
                    int tz_m = 0;
                    if (iss >> tz_h) {
                        if (iss.peek() == ':') {
                            iss >> tz_sep >> tz_m;
                        } else if (tz_h >= 100) {
                            tz_m = tz_h % 100;
                            tz_h = tz_h / 100;
                        }
                        int16_t offset = static_cast<int16_t>(tz_h * 60 + tz_m);
                        ts.tz_offset_minutes = (sign == '-') ? -offset : offset;
                    }
                }
            }
        }
        return ts;
    }

    std::chrono::system_clock::time_point to_time_point() const {
        std::chrono::year_month_day ymd{
            std::chrono::year{year},
            std::chrono::month{month},
            std::chrono::day{day}
        };
        auto tp = std::chrono::sys_days{ymd}
            + std::chrono::hours{hour}
            + std::chrono::minutes{minute}
            - std::chrono::minutes{tz_offset_minutes}
            + std::chrono::seconds{second}
            + std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::nanoseconds{fraction});
        return tp;
    }

    static SqlTimestamp from_time_point(std::chrono::system_clock::time_point tp) {
        auto dur = tp.time_since_epoch();
        auto sec_dur = std::chrono::floor<std::chrono::seconds>(dur);
        auto frac_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(dur - sec_dur).count();
        if (frac_ns < 0) frac_ns = 0;

        std::time_t t = sec_dur.count();
        std::tm tm{};
#if defined(_WIN32)
        gmtime_s(&tm, &t);
#else
        gmtime_r(&t, &tm);
#endif
        return SqlTimestamp(
            static_cast<int16_t>(tm.tm_year + 1900),
            static_cast<uint8_t>(tm.tm_mon + 1),
            static_cast<uint8_t>(tm.tm_mday),
            static_cast<uint8_t>(tm.tm_hour),
            static_cast<uint8_t>(tm.tm_min),
            static_cast<uint8_t>(tm.tm_sec),
            static_cast<uint32_t>(frac_ns)
        );
    }

    auto to_utc_tuple() const {
        std::chrono::year_month_day ymd{
            std::chrono::year{year},
            std::chrono::month{month},
            std::chrono::day{day}
        };
        int64_t days = std::chrono::sys_days{ymd}.time_since_epoch().count();
        int64_t total_minutes = days * 1440LL + static_cast<int64_t>(hour) * 60 + static_cast<int64_t>(minute) - tz_offset_minutes;
        return std::make_tuple(total_minutes, second, fraction);
    }

    bool operator==(const SqlTimestamp& other) const {
        return to_utc_tuple() == other.to_utc_tuple();
    }

    auto operator<=>(const SqlTimestamp& other) const {
        return to_utc_tuple() <=> other.to_utc_tuple();
    }
};

// ----------------------------------------------------------------------------
// SqlInterval: Year-Month or Day-Second Interval
// ----------------------------------------------------------------------------
enum class IntervalType {
    Year,
    Month,
    Day,
    Hour,
    Minute,
    Second,
    YearToMonth,
    DayToHour,
    DayToMinute,
    DayToSecond,
    HourToMinute,
    HourToSecond,
    MinuteToSecond
};

struct SqlInterval {
    IntervalType type = IntervalType::DayToSecond;
    bool is_negative = false;
    uint32_t years = 0;
    uint32_t months = 0;
    uint32_t days = 0;
    uint32_t hours = 0;
    uint32_t minutes = 0;
    uint32_t seconds = 0;
    uint32_t fraction = 0; // nanoseconds

    SqlInterval() = default;

    static SqlInterval from_year_month(uint32_t y, uint32_t m = 0, bool negative = false) {
        SqlInterval iv;
        iv.type = IntervalType::YearToMonth;
        iv.is_negative = negative;
        iv.years = y;
        iv.months = m;
        return iv;
    }

    static SqlInterval from_day_second(uint32_t d, uint32_t h = 0, uint32_t m = 0, uint32_t s = 0, uint32_t frac = 0, bool negative = false) {
        SqlInterval iv;
        iv.type = IntervalType::DayToSecond;
        iv.is_negative = negative;
        iv.days = d;
        iv.hours = h;
        iv.minutes = m;
        iv.seconds = s;
        iv.fraction = frac;
        return iv;
    }

    template <typename Rep, typename Period>
    static SqlInterval from_duration(const std::chrono::duration<Rep, Period>& dur) {
        auto total_ns_signed = std::chrono::duration_cast<std::chrono::nanoseconds>(dur).count();
        bool negative = total_ns_signed < 0;
        uint64_t total_ns = negative
            ? (0ULL - static_cast<uint64_t>(total_ns_signed))
            : static_cast<uint64_t>(total_ns_signed);

        uint64_t total_sec = total_ns / 1'000'000'000ULL;
        uint32_t frac = static_cast<uint32_t>(total_ns % 1'000'000'000ULL);

        uint32_t days = static_cast<uint32_t>(total_sec / 86400);
        uint32_t rem = static_cast<uint32_t>(total_sec % 86400);
        uint32_t hours = rem / 3600;
        rem %= 3600;
        uint32_t minutes = rem / 60;
        uint32_t seconds = rem % 60;

        return from_day_second(days, hours, minutes, seconds, frac, negative);
    }

    template <typename Duration = std::chrono::seconds>
    Duration to_duration() const {
        if (type == IntervalType::YearToMonth || type == IntervalType::Year || type == IntervalType::Month) {
            int64_t total_months = static_cast<int64_t>(years) * 12 + static_cast<int64_t>(months);
            if (is_negative) total_months = -total_months;
            return std::chrono::duration_cast<Duration>(std::chrono::months(total_months));
        }
        uint64_t total_sec = static_cast<uint64_t>(days) * 86400ULL +
                             static_cast<uint64_t>(hours) * 3600ULL +
                             static_cast<uint64_t>(minutes) * 60ULL +
                             static_cast<uint64_t>(seconds);
        uint64_t total_ns_u = total_sec * 1'000'000'000ULL + static_cast<uint64_t>(fraction);
        if (years > 0 || months > 0) {
            int64_t total_months = static_cast<int64_t>(years) * 12 + static_cast<int64_t>(months);
            int64_t total_ns_s = is_negative
                ? static_cast<int64_t>(0ULL - total_ns_u)
                : static_cast<int64_t>(total_ns_u);
            auto dur = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::months(is_negative ? -total_months : total_months)) +
                       std::chrono::nanoseconds(total_ns_s);
            return std::chrono::duration_cast<Duration>(dur);
        }
        int64_t total_ns = is_negative
            ? static_cast<int64_t>(0ULL - total_ns_u)
            : static_cast<int64_t>(total_ns_u);
        return std::chrono::duration_cast<Duration>(std::chrono::nanoseconds(total_ns));
    }

    std::string to_string() const {
        std::ostringstream oss;
        if (is_negative) oss << "-";
        if (type == IntervalType::YearToMonth || type == IntervalType::Year || type == IntervalType::Month) {
            oss << years << "-" << months;
        } else {
            oss << days << " "
                << std::setfill('0')
                << std::setw(2) << hours << ":"
                << std::setw(2) << minutes << ":"
                << std::setw(2) << seconds;
            if (fraction > 0) {
                oss << "." << std::setfill('0') << std::setw(9) << fraction;
            }
        }
        return oss.str();
    }

    static SqlInterval from_string(std::string_view str) {
        SqlInterval iv;
        if (str.empty()) return iv;
        std::string s(str);
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
        if (s.front() == '-') {
            iv.is_negative = true;
            s.erase(s.begin());
        } else if (s.front() == '+') {
            s.erase(s.begin());
        }

        auto parse_fraction = [](std::istringstream& iss) -> uint32_t {
            if (iss.peek() == '.') {
                iss.get();
                std::string frac_str;
                while (std::isdigit(static_cast<unsigned char>(iss.peek()))) {
                    frac_str.push_back(static_cast<char>(iss.get()));
                }
                if (!frac_str.empty()) {
                    if (frac_str.size() > 9) {
                        frac_str.resize(9);
                    } else {
                        while (frac_str.size() < 9) frac_str.push_back('0');
                    }
                    try { return static_cast<uint32_t>(std::stoul(frac_str)); } catch (...) {}
                }
            }
            return 0;
        };
        
        if (s.find('-') != std::string::npos && s.find(':') == std::string::npos && s.find("day") == std::string::npos) {
            iv.type = IntervalType::YearToMonth;
            int y = 0, mo = 0;
            char sep = 0;
            std::istringstream iss(s);
            if (iss >> y >> sep >> mo) {
                iv.years = static_cast<uint32_t>(y);
                iv.months = static_cast<uint32_t>(mo);
            }
        } else if (s.find("day") != std::string::npos || s.find("days") != std::string::npos) {
            iv.type = IntervalType::DayToSecond;
            int d = 0;
            std::istringstream iss(s);
            std::string day_word;
            iss >> d >> day_word;
            iv.days = static_cast<uint32_t>(d);
            int h = 0, mi = 0, sec = 0;
            char sep1 = 0, sep2 = 0;
            if (iss >> h >> sep1 >> mi >> sep2 >> sec) {
                iv.hours = static_cast<uint32_t>(h);
                iv.minutes = static_cast<uint32_t>(mi);
                iv.seconds = static_cast<uint32_t>(sec);
                iv.fraction = parse_fraction(iss);
            }
        } else {
            iv.type = IntervalType::DayToSecond;
            int d = 0, h = 0, mi = 0, sec = 0;
            if (s.find(' ') != std::string::npos) {
                std::istringstream iss(s);
                char sep1 = 0, sep2 = 0;
                if (iss >> d >> h >> sep1 >> mi >> sep2 >> sec) {
                    iv.days = static_cast<uint32_t>(d);
                    iv.hours = static_cast<uint32_t>(h);
                    iv.minutes = static_cast<uint32_t>(mi);
                    iv.seconds = static_cast<uint32_t>(sec);
                    iv.fraction = parse_fraction(iss);
                }
            } else if (s.find(':') != std::string::npos) {
                std::istringstream iss(s);
                char sep1 = 0, sep2 = 0;
                if (iss >> h >> sep1 >> mi >> sep2 >> sec) {
                    iv.hours = static_cast<uint32_t>(h);
                    iv.minutes = static_cast<uint32_t>(mi);
                    iv.seconds = static_cast<uint32_t>(sec);
                    iv.fraction = parse_fraction(iss);
                }
            }
        }
        return iv;
    }

    bool operator==(const SqlInterval& other) const = default;
};

// ----------------------------------------------------------------------------
// BoundValue: Type-erased bound parameter
// ----------------------------------------------------------------------------
using BoundValue = std::variant<
    std::monostate,           // NULL
    int64_t,
    uint64_t,
    double,
    std::string,
    std::wstring,
    bool,
    std::vector<uint8_t>,     // BLOB
    SqlNumeric,
    SqlDate,
    SqlTime,
    SqlTimestamp,
    SqlInterval,
    SqlGuid
>;

} // namespace cpplinq
