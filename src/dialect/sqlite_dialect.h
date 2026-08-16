#pragma once
#include "cpplinq/dialect/dialect.h"

namespace cpplinq {

class SqliteDialect : public ISqlDialect {
public:
    std::string quote_id(std::string_view id) const override;
    std::string placeholder(size_t index) const override;
    std::string limit_offset(std::optional<size_t> limit,
                             std::optional<size_t> offset) const override;
    std::string type_name(SqlType type) const override;
    std::string auto_increment_type() const override;
    std::string returning_clause(std::string_view column) const override;
    std::string extract_part_func(std::string_view part, std::string_view expr_sql) const override;
    std::string date_add_days_func(std::string_view expr_sql, std::string_view days_sql) const override;
};

} // namespace cpplinq
