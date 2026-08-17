#include "dialect/mysql_dialect.h"

#ifdef CPPLINQ_HAS_MYSQL

namespace cpplinq {

std::string MysqlDialect::quote_id(std::string_view id) const {
    std::string quoted = "`";
    for (char c : id) {
        if (c == '`') {
            quoted += "``";
        } else {
            quoted += c;
        }
    }
    quoted += "`";
    return quoted;
}

std::string MysqlDialect::placeholder(size_t /*index*/) const {
    return "?";
}

std::string MysqlDialect::limit_offset(std::optional<size_t> limit,
                                       std::optional<size_t> offset) const {
    std::string result;
    if (limit.has_value() && offset.has_value()) {
        result = " LIMIT " + std::to_string(*limit) + " OFFSET " + std::to_string(*offset);
    } else if (limit.has_value()) {
        result = " LIMIT " + std::to_string(*limit);
    } else if (offset.has_value()) {
        result = " LIMIT 18446744073709551615 OFFSET " + std::to_string(*offset);
    }
    return result;
}

std::string MysqlDialect::type_name(SqlType type) const {
    switch (type) {
        case SqlType::Integer:        return "INT";
        case SqlType::BigInt:         return "BIGINT";
        case SqlType::UnsignedBigInt: return "BIGINT UNSIGNED";
        case SqlType::Real:           return "DOUBLE";
        case SqlType::Decimal:        return "DECIMAL(28, 10)";
        case SqlType::Text:           return "VARCHAR(255)";
        case SqlType::WString:        return "VARCHAR(255) CHARACTER SET utf8mb4";
        case SqlType::Blob:           return "LONGBLOB";
        case SqlType::Boolean:        return "TINYINT(1)";
        case SqlType::Date:           return "DATE";
        case SqlType::Time:           return "TIME";
        case SqlType::Timestamp:      return "DATETIME";
        case SqlType::Interval:       return "VARCHAR(100)";
        case SqlType::Guid:           return "VARCHAR(36)";
    }
    return "VARCHAR(255)";
}

std::string MysqlDialect::auto_increment_type() const {
    return "INT AUTO_INCREMENT PRIMARY KEY";
}

std::string MysqlDialect::returning_clause(std::string_view /*column*/) const {
    return "";
}

std::string MysqlDialect::output_clause(std::string_view /*column*/) const {
    return "";
}

std::string MysqlDialect::create_table_prefix(std::string_view table_name) const {
    return "CREATE TABLE IF NOT EXISTS " + quote_id(table_name);
}

std::string MysqlDialect::function_name(std::string_view func) const {
    if (func == "SUBSTR") return "SUBSTRING";
    return std::string(func);
}

std::string MysqlDialect::current_timestamp_func() const {
    return "CURRENT_TIMESTAMP";
}

std::string MysqlDialect::current_date_func() const {
    return "CURRENT_DATE()";
}

std::string MysqlDialect::extract_part_func(std::string_view part, std::string_view expr_sql) const {
    if (part == "YEAR") return "YEAR(" + std::string(expr_sql) + ")";
    if (part == "MONTH") return "MONTH(" + std::string(expr_sql) + ")";
    if (part == "DAY") return "DAY(" + std::string(expr_sql) + ")";
    return "EXTRACT(" + std::string(part) + " FROM " + std::string(expr_sql) + ")";
}

std::string MysqlDialect::date_add_days_func(std::string_view expr_sql, std::string_view days_sql) const {
    return "DATE_ADD(" + std::string(expr_sql) + ", INTERVAL (" + std::string(days_sql) + ") DAY)";
}

std::string MysqlDialect::generate_upsert(
    std::string_view table_name,
    const std::vector<std::string>& insert_columns,
    const std::vector<std::string>& /*conflict_columns*/,
    const std::vector<std::string>& update_columns
) const {
    std::string sql = "INSERT INTO " + quote_id(table_name) + " (";
    for (size_t i = 0; i < insert_columns.size(); ++i) {
        if (i > 0) sql += ", ";
        sql += quote_id(insert_columns[i]);
    }
    sql += ") VALUES (";
    for (size_t i = 0; i < insert_columns.size(); ++i) {
        if (i > 0) sql += ", ";
        sql += placeholder(i);
    }
    sql += ") ON DUPLICATE KEY UPDATE ";
    for (size_t i = 0; i < update_columns.size(); ++i) {
        if (i > 0) sql += ", ";
        sql += quote_id(update_columns[i]) + " = VALUES(" + quote_id(update_columns[i]) + ")";
    }
    return sql;
}

} // namespace cpplinq

#endif // CPPLINQ_HAS_MYSQL
