#pragma once
#include "cpplinq/driver/connection.h"
#include "cpplinq/mapping/row_mapper.h"
#include "cpplinq/core/chunked_buffer.h"
#include "cpplinq/core/streaming.h"
#include "cpplinq/core/sql_generator.h"
#include <memory>
#include <vector>
#include <tuple>
#include <optional>
#include <type_traits>

namespace cpplinq {

// Helper trait to deduce RowMapper type from tuple of columns
template <typename Entity, typename TupleCols>
struct tuple_row_mapper_type;

template <typename Entity, typename... ColumnDefs>
struct tuple_row_mapper_type<Entity, std::tuple<ColumnDefs...>> {
    using type = RowMapper<Entity, ColumnDefs...>;
};

template <typename Entity, typename TupleCols>
using tuple_row_mapper_t = typename tuple_row_mapper_type<Entity, TupleCols>::type;

// Convert any standard type or optional to BoundValue
template <typename T>
BoundValue to_bound_value(const T& val) {
    using U = std::decay_t<T>;
    if constexpr (is_nullable_v<U>) {
        if (!val.has_value()) return BoundValue{std::monostate{}};
        return convert_to_bound_value(*val);
    } else if constexpr (std::is_same_v<U, BoundValue>) {
        return val;
    } else {
        return convert_to_bound_value(val);
    }
}

// Reusable prepared query for SELECT operations
template <typename Entity, typename TupleCols, typename... ParamTypes>
class PreparedQuery {
public:
    using Mapper = tuple_row_mapper_t<Entity, TupleCols>;

    PreparedQuery(std::unique_ptr<IPreparedStatement> stmt,
                  TupleCols columns,
                  std::vector<ParameterSlot> slots)
        : stmt_(std::move(stmt)),
          columns_(std::move(columns)),
          mapper_(columns_),
          slots_(std::move(slots)) {}

    PreparedQuery(const PreparedQuery&) = delete;
    PreparedQuery& operator=(const PreparedQuery&) = delete;
    PreparedQuery(PreparedQuery&&) noexcept = default;
    PreparedQuery& operator=(PreparedQuery&&) noexcept = default;
    ~PreparedQuery() = default;

    // Execute query and return typed ChunkedList<Entity, 64>
    template <typename... Args>
    ChunkedList<Entity, 64> execute(const Args&... args) {
        bind_and_execute(args...);
        auto reader = stmt_->execute_query();
        ChunkedList<Entity, 64> list;
        if (reader) {
            while (reader->next()) {
                mapper_.map_row(*reader, list.emplace_back());
            }
        }
        return list;
    }

    // Alias for execute: to_list
    template <typename... Args>
    ChunkedList<Entity, 64> to_list(const Args&... args) {
        return execute(args...);
    }

    // Return first matching entity, or std::nullopt
    template <typename... Args>
    std::optional<Entity> first(const Args&... args) {
        bind_and_execute(args...);
        auto reader = stmt_->execute_query();
        if (reader && reader->next()) {
            Entity e{};
            mapper_.map_row(*reader, e);
            return e;
        }
        return std::nullopt;
    }

    // Stream query results as a C++20 input range
    template <typename... Args>
    auto stream(const Args&... args) {
        bind_and_execute(args...);
        auto reader = stmt_->execute_query();
        return EntityStream<Entity, Mapper>(
            nullptr, std::move(reader), mapper_, ExecutionOptions{}
        );
    }

    // Stream with custom execution options
    template <typename... Args>
    auto stream_with_options(ExecutionOptions options, const Args&... args) {
        if (options.query_timeout_seconds.has_value()) {
            stmt_->set_timeout(*options.query_timeout_seconds);
        }
        if (options.stop_token.has_value()) {
            stmt_->set_stop_token(*options.stop_token);
        }
        bind_and_execute(args...);
        auto reader = stmt_->execute_query();
        return EntityStream<Entity, Mapper>(
            nullptr, std::move(reader), mapper_, std::move(options)
        );
    }

    // Access to underlying prepared statement
    IPreparedStatement& statement() { return *stmt_; }
    const IPreparedStatement& statement() const { return *stmt_; }

private:
    std::unique_ptr<IPreparedStatement> stmt_;
    TupleCols columns_;
    Mapper mapper_;
    std::vector<ParameterSlot> slots_;

    template <typename... Args>
    void bind_and_execute(const Args&... args) {
        if (!stmt_) {
            throw DbException("PreparedQuery statement is null or moved");
        }
        stmt_->reset();

        std::vector<BoundValue> dynamic_args;
        if constexpr (sizeof...(Args) > 0) {
            dynamic_args.reserve(sizeof...(Args));
            (dynamic_args.push_back(to_bound_value(args)), ...);
        }

        for (size_t i = 0; i < slots_.size(); ++i) {
            const auto& slot = slots_[i];
            if (slot.is_dynamic) {
                if (slot.dynamic_index < dynamic_args.size()) {
                    stmt_->bind(static_cast<int>(i), dynamic_args[slot.dynamic_index]);
                } else {
                    stmt_->bind(static_cast<int>(i), BoundValue{std::monostate{}});
                }
            } else {
                stmt_->bind(static_cast<int>(i), slot.static_value);
            }
        }
    }
};

// Reusable prepared command for UPDATE / DELETE operations
template <typename... ParamTypes>
class PreparedCommand {
public:
    PreparedCommand(std::unique_ptr<IPreparedStatement> stmt, std::vector<ParameterSlot> slots)
        : stmt_(std::move(stmt)), slots_(std::move(slots)) {}

    PreparedCommand(const PreparedCommand&) = delete;
    PreparedCommand& operator=(const PreparedCommand&) = delete;
    PreparedCommand(PreparedCommand&&) noexcept = default;
    PreparedCommand& operator=(PreparedCommand&&) noexcept = default;
    ~PreparedCommand() = default;

    template <typename... Args>
    size_t execute(const Args&... args) {
        bind_slots(args...);
        return stmt_->execute_non_query();
    }

    IPreparedStatement& statement() { return *stmt_; }
    const IPreparedStatement& statement() const { return *stmt_; }

private:
    std::unique_ptr<IPreparedStatement> stmt_;
    std::vector<ParameterSlot> slots_;

    template <typename... Args>
    void bind_slots(const Args&... args) {
        if (!stmt_) {
            throw DbException("PreparedCommand statement is null or moved");
        }
        stmt_->reset();
        std::vector<BoundValue> dynamic_args;
        if constexpr (sizeof...(Args) > 0) {
            dynamic_args.reserve(sizeof...(Args));
            (dynamic_args.push_back(to_bound_value(args)), ...);
        }
        for (size_t i = 0; i < slots_.size(); ++i) {
            const auto& slot = slots_[i];
            if (slot.is_dynamic) {
                if (slot.dynamic_index < dynamic_args.size()) {
                    stmt_->bind(static_cast<int>(i), dynamic_args[slot.dynamic_index]);
                } else {
                    stmt_->bind(static_cast<int>(i), BoundValue{std::monostate{}});
                }
            } else {
                stmt_->bind(static_cast<int>(i), slot.static_value);
            }
        }
    }
};

} // namespace cpplinq
