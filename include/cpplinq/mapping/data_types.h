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
#include <cctype>
#include <cmath>
#include <ctime>

namespace cpplinq {

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
        if (val < 0) {
            is_negative = true;
            val = -val;
        } else {
            is_negative = false;
        }
        value = (is_negative ? "-" : "") + std::to_string(static_cast<uint64_t>(val));
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
        if (std::isnan(val)) {
            value = "0";
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

    bool operator==(const SqlNumeric& other) const {
        return to_string() == other.to_string();
    }

    auto operator<=>(const SqlNumeric& other) const {
        double d1 = to_double();
        double d2 = other.to_double();
        return d1 <=> d2;
    }

private:
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
        value = s;
        size_t dot = s.find('.');
        if (dot != std::string::npos) {
            scale = static_cast<int8_t>(s.size() - dot - 1);
            size_t digits_count = s.size() - 1 - (is_negative ? 1 : 0);
            precision = static_cast<uint8_t>(digits_count);
        } else {
            scale = 0;
            precision = static_cast<uint8_t>(s.size() - (is_negative ? 1 : 0));
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

    explicit SqlDate(std::string_view iso_str) {
        *this = from_string(iso_str);
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

    explicit SqlTime(std::string_view str) {
        *this = from_string(str);
    }

    std::string to_string(bool include_fraction = false) const {
        std::ostringstream oss;
        oss << std::setfill('0')
            << std::setw(2) << static_cast<int>(hour) << ":"
            << std::setw(2) << static_cast<int>(minute) << ":"
            << std::setw(2) << static_cast<int>(second);
        if (include_fraction || fraction > 0) {
            oss << "." << std::setw(6) << (fraction / 1000);
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
                char dot = 0;
                iss >> dot;
                std::string frac_str;
                iss >> frac_str;
                while (frac_str.size() < 9) frac_str.push_back('0');
                if (frac_str.size() > 9) frac_str.resize(9);
                try { t.fraction = static_cast<uint32_t>(std::stoul(frac_str)); } catch (...) {}
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
            oss << "." << std::setw(6) << (fraction / 1000);
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
                    char dot = 0;
                    iss >> dot;
                    std::string frac_str;
                    iss >> frac_str;
                    while (frac_str.size() < 9) frac_str.push_back('0');
                    if (frac_str.size() > 9) frac_str.resize(9);
                    try { ts.fraction = static_cast<uint32_t>(std::stoul(frac_str)); } catch (...) {}
                }
            }
        }
        return ts;
    }

    std::chrono::system_clock::time_point to_time_point() const {
        std::tm tm{};
        tm.tm_year = year - 1900;
        tm.tm_mon = month - 1;
        tm.tm_mday = day;
        tm.tm_hour = hour;
        tm.tm_min = minute;
        tm.tm_sec = second;
        tm.tm_isdst = -1;
#if defined(_WIN32)
        std::time_t t = _mkgmtime(&tm);
#else
        std::time_t t = timegm(&tm);
#endif
        if (t == -1) t = 0;
        auto tp = std::chrono::system_clock::from_time_t(t);
        tp += std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::nanoseconds(fraction));
        return tp;
    }

    static SqlTimestamp from_time_point(std::chrono::system_clock::time_point tp) {
        auto dur = tp.time_since_epoch();
        auto sec_dur = std::chrono::duration_cast<std::chrono::seconds>(dur);
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

    bool operator==(const SqlTimestamp& other) const = default;
    auto operator<=>(const SqlTimestamp& other) const = default;
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
        auto total_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(dur).count();
        bool negative = total_ns < 0;
        if (negative) total_ns = -total_ns;

        uint64_t total_sec = static_cast<uint64_t>(total_ns / 1'000'000'000LL);
        uint32_t frac = static_cast<uint32_t>(total_ns % 1'000'000'000LL);

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
        int64_t total_sec = static_cast<int64_t>(days) * 86400 +
                            static_cast<int64_t>(hours) * 3600 +
                            static_cast<int64_t>(minutes) * 60 +
                            static_cast<int64_t>(seconds);
        int64_t total_ns = total_sec * 1'000'000'000LL + static_cast<int64_t>(fraction);
        if (is_negative) total_ns = -total_ns;
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
                oss << "." << std::setw(6) << (fraction / 1000);
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
        
        if (s.find('-') != std::string::npos && s.find(':') == std::string::npos) {
            iv.type = IntervalType::YearToMonth;
            int y = 0, mo = 0;
            char sep = 0;
            std::istringstream iss(s);
            if (iss >> y >> sep >> mo) {
                iv.years = static_cast<uint32_t>(y);
                iv.months = static_cast<uint32_t>(mo);
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
                }
            } else if (s.find(':') != std::string::npos) {
                std::istringstream iss(s);
                char sep1 = 0, sep2 = 0;
                if (iss >> h >> sep1 >> mi >> sep2 >> sec) {
                    iv.hours = static_cast<uint32_t>(h);
                    iv.minutes = static_cast<uint32_t>(mi);
                    iv.seconds = static_cast<uint32_t>(sec);
                }
            }
        }
        return iv;
    }

    bool operator==(const SqlInterval& other) const = default;
};

} // namespace cpplinq
