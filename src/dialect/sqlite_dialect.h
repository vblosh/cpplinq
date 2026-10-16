#pragma once
#include "cpplinq/dialect/dialect.h"

namespace cpplinq {

class SqliteDialect : public ISqlDialect {
public:
    ~SqliteDialect() override = default;

    std::string quote_id(std::string_view id) const override;
    std::string placeholder(size_t index) const override;
    std::string limit_offset(std::optional<size_t> limit,
                             std::optional<size_t> offset) const override;
    std::string type_name(SqlType type) const override;
    std::string auto_increment_type() const override;
    std::string returning_clause(std::string_view column) const override;
    std::string extract_part_func(std::string_view part, std::string_view expr_sql) const override;
    std::string date_add_days_func(std::string_view expr_sql, std::string_view days_sql) const override;
    std::string last_insert_id_query(std::string_view table_name = "", std::string_view pk_col = "") const override {
        (void)table_name;
        (void)pk_col;
        return "SELECT last_insert_rowid()";
    }
    bool supports_truncate() const override { return false; }
};

} // namespace cpplinq
