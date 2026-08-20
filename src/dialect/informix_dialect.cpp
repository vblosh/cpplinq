#include "dialect/informix_dialect.h"

#ifdef CPPLINQ_HAS_INFORMIX

namespace cpplinq {

std::string InformixDialect::quote_id(std::string_view id) const {
    std::string quoted = "\"";
    for (char c : id) {
        if (c == '"') {
            quoted += "\"\"";
        } else {
            quoted += c;
        }
    }
    quoted += "\"";
    return quoted;
}

std::string InformixDialect::placeholder(size_t /*index*/) const {
    return "?";
}

std::string InformixDialect::limit_offset(std::optional<size_t> /*limit*/,
                                          std::optional<size_t> /*offset*/) const {
    return "";
}

std::string InformixDialect::select_prefix_limit_offset(std::optional<size_t> limit,
                                                       std::optional<size_t> offset) const {
    std::string result;
    if (offset.has_value()) {
        result += "SKIP " + std::to_string(*offset) + " ";
    }
    if (limit.has_value()) {
        result += "FIRST " + std::to_string(*limit) + " ";
    }
    return result;
}

std::string InformixDialect::type_name(SqlType type) const {
    switch (type) {
        case SqlType::Integer:        return "INTEGER";
        case SqlType::BigInt:         return "BIGINT";
        case SqlType::UnsignedBigInt: return "DECIMAL(20, 0)";
        case SqlType::Real:           return "FLOAT";
        case SqlType::Decimal:        return "DECIMAL(28, 10)";
        case SqlType::Text:           return "VARCHAR(255)";
        case SqlType::WString:        return "NVARCHAR(255)";
        case SqlType::Blob:           return "BLOB";
        case SqlType::Boolean:        return "BOOLEAN";
        case SqlType::Date:           return "DATE";
        case SqlType::Time:           return "DATETIME HOUR TO SECOND";
        case SqlType::Timestamp:      return "DATETIME YEAR TO FRACTION(3)";
        case SqlType::Interval:       return "VARCHAR(100)";
        case SqlType::Guid:           return "VARCHAR(36)";
    }
    return "VARCHAR(255)";
}

std::string InformixDialect::auto_increment_type() const {
    return "SERIAL PRIMARY KEY";
}

std::string InformixDialect::returning_clause(std::string_view /*column*/) const {
    return "";
}

std::string InformixDialect::output_clause(std::string_view /*column*/) const {
    return "";
}

std::string InformixDialect::last_insert_id_query(std::string_view /*table_name*/, std::string_view /*pk_col*/) const {
    return "SELECT DBINFO('sqlca.sqlerrd1') FROM \"informix\".systables WHERE tabid = 1";
}

std::string InformixDialect::create_table_prefix(std::string_view table_name) const {
    return "CREATE TABLE IF NOT EXISTS " + quote_id(table_name);
}

std::string InformixDialect::function_name(std::string_view func) const {
    if (func == "SUBSTR") return "SUBSTR";
    return std::string(func);
}

std::string InformixDialect::current_timestamp_func() const {
    return "CURRENT YEAR TO FRACTION(3)";
}

std::string InformixDialect::current_date_func() const {
    return "TODAY";
}

std::string InformixDialect::extract_part_func(std::string_view part, std::string_view expr_sql) const {
    if (part == "YEAR") return "YEAR(CAST(" + std::string(expr_sql) + " AS DATETIME YEAR TO SECOND))";
    if (part == "MONTH") return "MONTH(CAST(" + std::string(expr_sql) + " AS DATETIME YEAR TO SECOND))";
    if (part == "DAY") return "DAY(CAST(" + std::string(expr_sql) + " AS DATETIME YEAR TO SECOND))";
    return "EXTRACT(" + std::string(part) + " FROM CAST(" + std::string(expr_sql) + " AS DATETIME YEAR TO SECOND))";
}

std::string InformixDialect::date_add_days_func(std::string_view expr_sql, std::string_view days_sql) const {
    return "(" + std::string(expr_sql) + " + (" + std::string(days_sql) + ") UNITS DAY)";
}

namespace {
std::string get_informix_cast_type(const BoundValue& val) {
    return std::visit([](const auto& v) -> std::string {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, int64_t>) {
            return "BIGINT";
        } else if constexpr (std::is_same_v<T, double>) {
            return "FLOAT";
        } else if constexpr (std::is_same_v<T, std::string>) {
            return "VARCHAR(255)";
        } else if constexpr (std::is_same_v<T, std::wstring>) {
            return "NVARCHAR(255)";
        } else if constexpr (std::is_same_v<T, bool>) {
            return "BOOLEAN";
        } else if constexpr (std::is_same_v<T, SqlDate>) {
            return "DATE";
        } else if constexpr (std::is_same_v<T, SqlTime>) {
            return "DATETIME HOUR TO SECOND";
        } else if constexpr (std::is_same_v<T, SqlTimestamp>) {
            return "DATETIME YEAR TO FRACTION(3)";
        } else if constexpr (std::is_same_v<T, SqlNumeric>) {
            return "DECIMAL(28, 10)";
        } else if constexpr (std::is_same_v<T, SqlInterval>) {
            return "VARCHAR(100)";
        } else if constexpr (std::is_same_v<T, SqlGuid>) {
            return "VARCHAR(36)";
        }
        return "VARCHAR(255)";
    }, val);
}
} // namespace

std::string InformixDialect::generate_upsert(
    std::string_view table_name,
    const std::vector<std::string>& insert_columns,
    const std::vector<std::string>& conflict_columns,
    const std::vector<std::string>& update_columns
) const {
    std::vector<BoundValue> empty_values;
    return generate_upsert(table_name, insert_columns, empty_values, conflict_columns, update_columns);
}

std::string InformixDialect::generate_upsert(
    std::string_view table_name,
    const std::vector<std::string>& insert_columns,
    const std::vector<BoundValue>& values,
    const std::vector<std::string>& conflict_columns,
    const std::vector<std::string>& update_columns
) const {
    std::string sql = "MERGE INTO " + quote_id(table_name) + " AS target USING (SELECT ";
    for (size_t i = 0; i < insert_columns.size(); ++i) {
        if (i > 0) sql += ", ";
        std::string cast_type = (i < values.size()) ? get_informix_cast_type(values[i]) : "VARCHAR(255)";
        sql += "CAST(" + placeholder(i) + " AS " + cast_type + ") AS " + quote_id(insert_columns[i]);
    }
    sql += " FROM \"informix\".systables WHERE tabid = 1) AS source ON (";
    for (size_t i = 0; i < conflict_columns.size(); ++i) {
        if (i > 0) sql += " AND ";
        sql += "target." + quote_id(conflict_columns[i]) + " = source." + quote_id(conflict_columns[i]);
    }
    sql += ") WHEN MATCHED THEN UPDATE SET ";
    for (size_t i = 0; i < update_columns.size(); ++i) {
        if (i > 0) sql += ", ";
        sql += "target." + quote_id(update_columns[i]) + " = source." + quote_id(update_columns[i]);
    }
    sql += " WHEN NOT MATCHED THEN INSERT (";
    for (size_t i = 0; i < insert_columns.size(); ++i) {
        if (i > 0) sql += ", ";
        sql += quote_id(insert_columns[i]);
    }
    sql += ") VALUES (";
    for (size_t i = 0; i < insert_columns.size(); ++i) {
        if (i > 0) sql += ", ";
        sql += "source." + quote_id(insert_columns[i]);
    }
    sql += ")";
    return sql;
}

} // namespace cpplinq

#endif // CPPLINQ_HAS_INFORMIX
