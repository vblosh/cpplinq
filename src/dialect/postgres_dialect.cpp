#include "dialect/postgres_dialect.h"

#ifdef CPPLINQ_HAS_POSTGRES

namespace cpplinq {

std::string PostgresDialect::quote_id(std::string_view id) const {
    return "\"" + std::string(id) + "\"";
}

std::string PostgresDialect::placeholder(size_t /*index*/) const {
    return "?";
}

std::string PostgresDialect::limit_offset(std::optional<size_t> limit,
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

std::string PostgresDialect::type_name(SqlType type) const {
    switch (type) {
        case SqlType::Integer:        return "INTEGER";
        case SqlType::BigInt:         return "BIGINT";
        case SqlType::UnsignedBigInt: return "NUMERIC(20, 0)";
        case SqlType::Real:           return "DOUBLE PRECISION";
        case SqlType::Decimal:        return "NUMERIC";
        case SqlType::Text:           return "TEXT";
        case SqlType::WString:        return "TEXT";
        case SqlType::Blob:           return "BYTEA";
        case SqlType::Boolean:        return "BOOLEAN";
        case SqlType::Date:           return "DATE";
        case SqlType::Time:           return "TIME";
        case SqlType::Timestamp:      return "TIMESTAMP";
        case SqlType::Interval:       return "INTERVAL";
        case SqlType::Guid:           return "UUID";
    }
    return "TEXT";
}

std::string PostgresDialect::auto_increment_type() const {
    return "BIGSERIAL PRIMARY KEY";
}

std::string PostgresDialect::returning_clause(std::string_view column) const {
    return " RETURNING \"" + std::string(column) + "\"";
}

} // namespace cpplinq

#endif // CPPLINQ_HAS_POSTGRES
