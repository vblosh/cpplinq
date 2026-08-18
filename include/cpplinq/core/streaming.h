#pragma once

#include "cpplinq/driver/connection.h"
#include <memory>
#include <vector>
#include <string>
#include <optional>
#include <stop_token>
#include <functional>
#include <iterator>

namespace cpplinq {

// Generic single-row container for raw streaming
struct RowRecord {
    std::vector<BoundValue> values;

    int column_count() const {
        return static_cast<int>(values.size());
    }

    bool is_null(size_t col) const {
        if (col >= values.size()) return true;
        return std::holds_alternative<std::monostate>(values[col]);
    }

    int64_t get_int64(size_t col) const {
        if (col >= values.size()) return 0;
        if (auto* v = std::get_if<int64_t>(&values[col])) return *v;
        if (auto* v = std::get_if<uint64_t>(&values[col])) return static_cast<int64_t>(*v);
        if (auto* v = std::get_if<double>(&values[col])) return static_cast<int64_t>(*v);
        if (auto* v = std::get_if<bool>(&values[col])) return *v ? 1 : 0;
        if (auto* v = std::get_if<SqlNumeric>(&values[col])) return v->to_int64();
        if (auto* v = std::get_if<std::string>(&values[col])) {
            try { return std::stoll(*v); } catch (...) {}
        }
        return 0;
    }

    double get_double(size_t col) const {
        if (col >= values.size()) return 0.0;
        if (auto* v = std::get_if<double>(&values[col])) return *v;
        if (auto* v = std::get_if<int64_t>(&values[col])) return static_cast<double>(*v);
        if (auto* v = std::get_if<uint64_t>(&values[col])) return static_cast<double>(*v);
        if (auto* v = std::get_if<SqlNumeric>(&values[col])) return v->to_double();
        if (auto* v = std::get_if<std::string>(&values[col])) {
            try { return std::stod(*v); } catch (...) {}
        }
        return 0.0;
    }

    std::string get_string(size_t col) const {
        if (col >= values.size()) return "";
        if (auto* v = std::get_if<std::string>(&values[col])) return *v;
        if (auto* v = std::get_if<int64_t>(&values[col])) return std::to_string(*v);
        if (auto* v = std::get_if<uint64_t>(&values[col])) return std::to_string(*v);
        if (auto* v = std::get_if<double>(&values[col])) return std::to_string(*v);
        if (auto* v = std::get_if<bool>(&values[col])) return *v ? "true" : "false";
        if (auto* v = std::get_if<std::wstring>(&values[col])) return wstring_to_utf8(*v);
        if (auto* v = std::get_if<SqlNumeric>(&values[col])) return v->to_string();
        if (auto* v = std::get_if<SqlDate>(&values[col])) return v->to_string();
        if (auto* v = std::get_if<SqlTime>(&values[col])) return v->to_string();
        if (auto* v = std::get_if<SqlTimestamp>(&values[col])) return v->to_string(v->fraction > 0);
        if (auto* v = std::get_if<SqlInterval>(&values[col])) return v->to_string();
        if (auto* v = std::get_if<SqlGuid>(&values[col])) return v->to_string();
        return "";
    }

    bool get_bool(size_t col) const {
        if (col >= values.size()) return false;
        if (auto* v = std::get_if<bool>(&values[col])) return *v;
        if (auto* v = std::get_if<int64_t>(&values[col])) return *v != 0;
        if (auto* v = std::get_if<uint64_t>(&values[col])) return *v != 0;
        if (auto* v = std::get_if<double>(&values[col])) return *v != 0.0;
        if (auto* v = std::get_if<SqlNumeric>(&values[col])) return v->to_int64() != 0;
        if (auto* v = std::get_if<std::string>(&values[col])) {
            return !v->empty() && *v != "0" && *v != "false";
        }
        return false;
    }

    std::vector<uint8_t> get_blob(size_t col) const {
        if (col >= values.size()) return {};
        if (auto* v = std::get_if<std::vector<uint8_t>>(&values[col])) return *v;
        return {};
    }
};

// Move-only, single-pass range over raw database rows
class RowStream {
public:
    RowStream(
        std::unique_ptr<IPreparedStatement> stmt,
        std::unique_ptr<IDataReader> reader,
        ExecutionOptions options = {}
    ) : stmt_(std::move(stmt))
      , reader_(std::move(reader))
      , options_(std::move(options))
    {
        if (options_.stop_token.has_value() && options_.stop_token->stop_possible() && stmt_) {
            stop_cb_.emplace(*options_.stop_token, [stmt = stmt_.get()]() {
                try { stmt->cancel(); } catch (...) {}
            });
        }
    }

    RowStream(const RowStream&) = delete;
    RowStream& operator=(const RowStream&) = delete;

    RowStream(RowStream&& other) noexcept = default;
    RowStream& operator=(RowStream&& other) noexcept = default;

    ~RowStream() = default;

    class Sentinel {};

    class Iterator {
    public:
        using iterator_concept = std::input_iterator_tag;
        using iterator_category = std::input_iterator_tag;
        using value_type = RowRecord;
        using difference_type = std::ptrdiff_t;
        using pointer = const RowRecord*;
        using reference = const RowRecord&;

        Iterator() : stream_(nullptr), is_end_(true) {}

        explicit Iterator(RowStream* stream)
            : stream_(stream)
            , is_end_(false)
        {
            advance();
        }

        reference operator*() const {
            return current_row_;
        }

        pointer operator->() const {
            return &current_row_;
        }

        Iterator& operator++() {
            advance();
            return *this;
        }

        void operator++(int) {
            advance();
        }

        bool operator==(const Sentinel&) const noexcept {
            return is_end_;
        }
        bool operator!=(const Sentinel&) const noexcept {
            return !is_end_;
        }

        bool operator==(const Iterator& other) const noexcept {
            if (is_end_ && other.is_end_) return true;
            return is_end_ == other.is_end_ && stream_ == other.stream_;
        }
        bool operator!=(const Iterator& other) const noexcept {
            return !(*this == other);
        }

    private:
        void advance() {
            if (is_end_ || !stream_ || !stream_->reader_) {
                is_end_ = true;
                return;
            }

            if (stream_->options_.stop_token.has_value() && stream_->options_.stop_token->stop_requested()) {
                is_end_ = true;
                throw OperationCancelled("Streaming cancelled by stop_token");
            }

            if (!stream_->reader_->next()) {
                is_end_ = true;
                return;
            }

            // Populate current_row_
            int cols = stream_->reader_->column_count();
            current_row_.values.clear();
            current_row_.values.reserve(cols);

            for (int i = 0; i < cols; ++i) {
                if (stream_->reader_->is_null(i)) {
                    current_row_.values.emplace_back(std::monostate{});
                } else {
                    current_row_.values.emplace_back(stream_->reader_->get_value(i));
                }
            }
        }

        RowStream* stream_ = nullptr;
        bool is_end_ = true;
        RowRecord current_row_;
    };

    Iterator begin() {
        return Iterator(this);
    }

    Sentinel end() const noexcept {
        return Sentinel{};
    }

private:
    std::unique_ptr<IPreparedStatement> stmt_;
    std::unique_ptr<IDataReader> reader_;
    ExecutionOptions options_;
    std::optional<std::stop_callback<std::function<void()>>> stop_cb_;
};

// Move-only, single-pass range over mapped C++ entities / tuples
template <typename Entity, typename Mapper>
class EntityStream {
public:
    EntityStream(
        std::unique_ptr<IPreparedStatement> stmt,
        std::unique_ptr<IDataReader> reader,
        Mapper mapper,
        ExecutionOptions options = {}
    ) : stmt_(std::move(stmt))
      , reader_(std::move(reader))
      , mapper_(std::move(mapper))
      , options_(std::move(options))
    {
        if (options_.stop_token.has_value() && options_.stop_token->stop_possible() && stmt_) {
            stop_cb_.emplace(*options_.stop_token, [stmt = stmt_.get()]() {
                try { stmt->cancel(); } catch (...) {}
            });
        }
    }

    EntityStream(const EntityStream&) = delete;
    EntityStream& operator=(const EntityStream&) = delete;

    EntityStream(EntityStream&&) noexcept = default;
    EntityStream& operator=(EntityStream&&) noexcept = default;

    ~EntityStream() = default;

    class Sentinel {};

    class Iterator {
    public:
        using iterator_concept = std::input_iterator_tag;
        using iterator_category = std::input_iterator_tag;
        using value_type = Entity;
        using difference_type = std::ptrdiff_t;
        using pointer = const Entity*;
        using reference = const Entity&;

        Iterator() : stream_(nullptr), is_end_(true) {}

        explicit Iterator(EntityStream* stream)
            : stream_(stream)
            , is_end_(false)
        {
            advance();
        }

        reference operator*() const {
            return current_entity_;
        }

        pointer operator->() const {
            return &current_entity_;
        }

        Iterator& operator++() {
            advance();
            return *this;
        }

        void operator++(int) {
            advance();
        }

        bool operator==(const Sentinel&) const noexcept {
            return is_end_;
        }
        bool operator!=(const Sentinel&) const noexcept {
            return !is_end_;
        }

        bool operator==(const Iterator& other) const noexcept {
            if (is_end_ && other.is_end_) return true;
            return is_end_ == other.is_end_ && stream_ == other.stream_;
        }
        bool operator!=(const Iterator& other) const noexcept {
            return !(*this == other);
        }

    private:
        void advance() {
            if (is_end_ || !stream_ || !stream_->reader_) {
                is_end_ = true;
                return;
            }

            if (stream_->options_.stop_token.has_value() && stream_->options_.stop_token->stop_requested()) {
                is_end_ = true;
                throw OperationCancelled("Streaming cancelled by stop_token");
            }

            if (!stream_->reader_->next()) {
                is_end_ = true;
                return;
            }

            current_entity_ = stream_->mapper_.map_row(*stream_->reader_);
        }

        EntityStream* stream_ = nullptr;
        bool is_end_ = true;
        Entity current_entity_{};
    };

    Iterator begin() {
        return Iterator(this);
    }

    Sentinel end() const noexcept {
        return Sentinel{};
    }

private:
    std::unique_ptr<IPreparedStatement> stmt_;
    std::unique_ptr<IDataReader> reader_;
    Mapper mapper_;
    ExecutionOptions options_;
    std::optional<std::stop_callback<std::function<void()>>> stop_cb_;
};

} // namespace cpplinq
