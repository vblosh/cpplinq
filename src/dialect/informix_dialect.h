#pragma once
#include "cpplinq/dialect/dialect.h"

namespace cpplinq {

class InformixDialect : public ISqlDialect {
public:
    ~InformixDialect() override = default;

    std::string quote_id(std::string_view id) const override;
    std::string placeholder(size_t index) const override;
    std::string limit_offset(std::optional<size_t> limit,
                             std::optional<size_t> offset) const override;
    std::string select_prefix_limit_offset(std::optional<size_t> limit,
                                          std::optional<size_t> offset) const override;
    std::string type_name(SqlType type) const override;
    std::string auto_increment_type() const override;
    std::string returning_clause(std::string_view column) const override;
    std::string output_clause(std::string_view column) const override;
    std::string last_insert_id_query() const override;
    std::string create_table_prefix(std::string_view table_name) const override;
    std::string function_name(std::string_view func) const override;
    std::string current_timestamp_func() const override;
    std::string current_date_func() const override;
    std::string extract_part_func(std::string_view part, std::string_view expr_sql) const override;
    std::string date_add_days_func(std::string_view expr_sql, std::string_view days_sql) const override;
    std::string generate_upsert(
        std::string_view table_name,
        const std::vector<std::string>& insert_columns,
        const std::vector<std::string>& conflict_columns,
        const std::vector<std::string>& update_columns
    ) const override;
    std::string generate_upsert(
        std::string_view table_name,
        const std::vector<std::string>& insert_columns,
        const std::vector<BoundValue>& values,
        const std::vector<std::string>& conflict_columns,
        const std::vector<std::string>& update_columns
    ) const override;
};

} // namespace cpplinq
