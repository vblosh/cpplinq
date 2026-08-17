#include "cpplinq/driver/connection.h"
#include "cpplinq/core/streaming.h"

namespace cpplinq {

RowStream IConnection::stream(
    std::string_view sql,
    const std::vector<BoundValue>& params,
    ExecutionOptions options
) {
    auto stmt = prepare(sql);
    if (options.query_timeout_seconds.has_value()) {
        stmt->set_timeout(*options.query_timeout_seconds);
    }
    if (options.stop_token.has_value()) {
        stmt->set_stop_token(*options.stop_token);
    }
    for (size_t i = 0; i < params.size(); ++i) {
        stmt->bind(static_cast<int>(i), params[i]);
    }
    auto reader = stmt->execute_query();
    return RowStream(std::move(stmt), std::move(reader), std::move(options));
}

size_t IConnection::insert_many_batch(
    std::string_view sql,
    const std::vector<BoundValue>& flat_params,
    size_t col_count,
    size_t row_count
) {
    if (row_count == 0 || col_count == 0) return 0;
    size_t total = 0;
    for (size_t r = 0; r < row_count; ++r) {
        auto stmt = prepare(sql);
        for (size_t c = 0; c < col_count; ++c) {
            stmt->bind(static_cast<int>(c), flat_params[r * col_count + c]);
        }
        total += stmt->execute_non_query();
    }
    return total;
}

} // namespace cpplinq
