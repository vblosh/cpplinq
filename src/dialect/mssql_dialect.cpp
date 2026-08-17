#include "dialect/mssql_dialect.h"

namespace cpplinq {

std::string MssqlDialect::quote_id(std::string_view id) const {
    std::string quoted = "[";
    for (char c : id) {
        if (c == ']') {
            quoted += "]]";
        } else {
            quoted += c;
        }
    }
    quoted += "]";
    return quoted;
}

std::string MssqlDialect::placeholder(size_t /*index*/) const {
    return "?";
}

std::string MssqlDialect::limit_offset(std::optional<size_t> limit,
                                       std::optional<size_t> offset) const {
    std::string result;
    if (offset.has_value() && limit.has_value()) {
        result = " OFFSET " + std::to_string(*offset) + " ROWS FETCH NEXT " + std::to_string(*limit) + " ROWS ONLY";
    } else if (offset.has_value()) {
        result = " OFFSET " + std::to_string(*offset) + " ROWS";
    } else if (limit.has_value()) {
        result = " OFFSET 0 ROWS FETCH NEXT " + std::to_string(*limit) + " ROWS ONLY";
    }
    return result;
}

std::string MssqlDialect::type_name(SqlType type) const {
    switch (type) {
        case SqlType::Integer:        return "INT";
        case SqlType::BigInt:         return "BIGINT";
        case SqlType::UnsignedBigInt: return "NUMERIC(20, 0)";
        case SqlType::Real:           return "FLOAT";
        case SqlType::Decimal:        return "DECIMAL(28, 10)";
        case SqlType::Text:           return "NVARCHAR(255)";
        case SqlType::WString:        return "NVARCHAR(255)";
        case SqlType::Blob:           return "VARBINARY(MAX)";
        case SqlType::Boolean:        return "BIT";
        case SqlType::Date:           return "DATE";
        case SqlType::Time:           return "TIME";
        case SqlType::Timestamp:      return "DATETIME2";
        case SqlType::Interval:       return "VARCHAR(100)";
        case SqlType::Guid:           return "UNIQUEIDENTIFIER";
    }
    return "NVARCHAR(255)";
}

std::string MssqlDialect::auto_increment_type() const {
    return "INT IDENTITY(1,1) PRIMARY KEY";
}

std::string MssqlDialect::returning_clause(std::string_view /*column*/) const {
    return "";
}

std::string MssqlDialect::output_clause(std::string_view column) const {
    return " OUTPUT INSERTED." + quote_id(column);
}

std::string MssqlDialect::create_table_prefix(std::string_view table_name) const {
    return "IF OBJECT_ID(N'" + std::string(table_name) + "', N'U') IS NULL CREATE TABLE " + quote_id(table_name);
}

std::string MssqlDialect::function_name(std::string_view func) const {
    if (func == "LENGTH") return "LEN";
    if (func == "SUBSTR") return "SUBSTRING";
    return std::string(func);
}

std::string MssqlDialect::generate_upsert(
    std::string_view table_name,
    const std::vector<std::string>& insert_columns,
    const std::vector<std::string>& conflict_columns,
    const std::vector<std::string>& update_columns
) const {
    std::string sql = "MERGE INTO " + quote_id(table_name) + " WITH (HOLDLOCK) AS [target] USING (VALUES (";
    for (size_t i = 0; i < insert_columns.size(); ++i) {
        if (i > 0) sql += ", ";
        sql += placeholder(i);
    }
    sql += ")) AS [source] (";
    for (size_t i = 0; i < insert_columns.size(); ++i) {
        if (i > 0) sql += ", ";
        sql += quote_id(insert_columns[i]);
    }
    sql += ") ON (";
    for (size_t i = 0; i < conflict_columns.size(); ++i) {
        if (i > 0) sql += " AND ";
        sql += "[target]." + quote_id(conflict_columns[i]) + " = [source]." + quote_id(conflict_columns[i]);
    }
    sql += ") WHEN MATCHED THEN UPDATE SET ";
    for (size_t i = 0; i < update_columns.size(); ++i) {
        if (i > 0) sql += ", ";
        sql += "[target]." + quote_id(update_columns[i]) + " = [source]." + quote_id(update_columns[i]);
    }
    sql += " WHEN NOT MATCHED THEN INSERT (";
    for (size_t i = 0; i < insert_columns.size(); ++i) {
        if (i > 0) sql += ", ";
        sql += quote_id(insert_columns[i]);
    }
    sql += ") VALUES (";
    for (size_t i = 0; i < insert_columns.size(); ++i) {
        if (i > 0) sql += ", ";
        sql += "[source]." + quote_id(insert_columns[i]);
    }
    sql += ");";
    return sql;
}

std::string MssqlDialect::current_date_func() const {
    return "CAST(GETDATE() AS DATE)";
}

std::string MssqlDialect::extract_part_func(std::string_view part, std::string_view expr_sql) const {
    if (part == "YEAR") return "YEAR(" + std::string(expr_sql) + ")";
    if (part == "MONTH") return "MONTH(" + std::string(expr_sql) + ")";
    if (part == "DAY") return "DAY(" + std::string(expr_sql) + ")";
    return "DATEPART(" + std::string(part) + ", " + std::string(expr_sql) + ")";
}

std::string MssqlDialect::date_add_days_func(std::string_view expr_sql, std::string_view days_sql) const {
    return "DATEADD(day, " + std::string(days_sql) + ", " + std::string(expr_sql) + ")";
}

} // namespace cpplinq
