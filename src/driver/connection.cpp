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

} // namespace cpplinq
