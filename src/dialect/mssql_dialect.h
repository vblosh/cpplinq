#pragma once
#include "cpplinq/dialect/dialect.h"

namespace cpplinq {

class MssqlDialect : public ISqlDialect {
public:
    ~MssqlDialect() override = default;

    std::string quote_id(std::string_view id) const override;
    std::string placeholder(size_t index) const override;
    std::string limit_offset(std::optional<size_t> limit,
                             std::optional<size_t> offset) const override;
    std::string type_name(SqlType type) const override;
    std::string auto_increment_type() const override;
    std::string returning_clause(std::string_view column) const override;
    std::string output_clause(std::string_view column) const override;
    std::string create_table_prefix(std::string_view table_name) const override;
    std::string function_name(std::string_view func) const override;
};

} // namespace cpplinq
