#include "dialect/sqlite_dialect.h"

namespace cpplinq {

std::string SqliteDialect::quote_id(std::string_view id) const {
    return "\"" + std::string(id) + "\"";
}

std::string SqliteDialect::placeholder(size_t /*index*/) const {
    return "?";
}

std::string SqliteDialect::limit_offset(std::optional<size_t> limit,
                                        std::optional<size_t> offset) const {
    std::string result;
    if (limit.has_value()) {
        result += " LIMIT " + std::to_string(*limit);
    }
    if (offset.has_value()) {
        result += " OFFSET " + std::to_string(*offset);
    }
    return result;
}

std::string SqliteDialect::type_name(SqlType type) const {
    switch (type) {
        case SqlType::Integer:        return "INTEGER";
        case SqlType::BigInt:         return "INTEGER";
        case SqlType::UnsignedBigInt: return "INTEGER";
        case SqlType::Real:           return "REAL";
        case SqlType::Decimal:        return "TEXT";
        case SqlType::Text:           return "TEXT";
        case SqlType::WString:        return "TEXT";
        case SqlType::Blob:           return "BLOB";
        case SqlType::Boolean:        return "INTEGER";
        case SqlType::Date:           return "TEXT";
        case SqlType::Time:           return "TEXT";
        case SqlType::Timestamp:      return "TEXT";
        case SqlType::Interval:       return "TEXT";
        case SqlType::Guid:           return "TEXT";
    }
    return "TEXT";
}

std::string SqliteDialect::auto_increment_type() const {
    return "INTEGER PRIMARY KEY AUTOINCREMENT";
}

std::string SqliteDialect::returning_clause(std::string_view column) const {
    return " RETURNING \"" + std::string(column) + "\"";
}

std::string SqliteDialect::extract_part_func(std::string_view part, std::string_view expr_sql) const {
    if (part == "YEAR") return "CAST(strftime('%Y', " + std::string(expr_sql) + ") AS INTEGER)";
    if (part == "MONTH") return "CAST(strftime('%m', " + std::string(expr_sql) + ") AS INTEGER)";
    if (part == "DAY") return "CAST(strftime('%d', " + std::string(expr_sql) + ") AS INTEGER)";
    return "strftime('" + std::string(part) + "', " + std::string(expr_sql) + ")";
}

std::string SqliteDialect::date_add_days_func(std::string_view expr_sql, std::string_view days_sql) const {
    return "date(" + std::string(expr_sql) + ", '+' || (" + std::string(days_sql) + ") || ' days')";
}

} // namespace cpplinq
