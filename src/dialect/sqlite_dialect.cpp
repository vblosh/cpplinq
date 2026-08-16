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
        case SqlType::Integer: return "INTEGER";
        case SqlType::BigInt:  return "INTEGER";
        case SqlType::Real:    return "REAL";
        case SqlType::Text:    return "TEXT";
        case SqlType::Blob:    return "BLOB";
        case SqlType::Boolean: return "INTEGER";
    }
    return "TEXT";
}

std::string SqliteDialect::auto_increment_type() const {
    return "INTEGER PRIMARY KEY AUTOINCREMENT";
}

std::string SqliteDialect::returning_clause(std::string_view column) const {
    return " RETURNING \"" + std::string(column) + "\"";
}

} // namespace cpplinq
