#pragma once

#include <vector>
#include <memory>
#include <utility>
#include <cstddef>
#include <new>
#include <type_traits>

namespace cpplinq {

template <typename T, size_t ChunkSize = 64>
class ChunkedBuffer {
public:
    ChunkedBuffer() = default;

    ~ChunkedBuffer() {
        clear();
    }

    ChunkedBuffer(const ChunkedBuffer&) = delete;
    ChunkedBuffer& operator=(const ChunkedBuffer&) = delete;

    ChunkedBuffer(ChunkedBuffer&& other) noexcept
        : chunks_(std::move(other.chunks_))
        , current_chunk_count_(other.current_chunk_count_)
        , total_size_(other.total_size_)
    {
        other.current_chunk_count_ = 0;
        other.total_size_ = 0;
    }

    ChunkedBuffer& operator=(ChunkedBuffer&& other) noexcept {
        if (this != &other) {
            clear();
            chunks_ = std::move(other.chunks_);
            current_chunk_count_ = other.current_chunk_count_;
            total_size_ = other.total_size_;
            other.current_chunk_count_ = 0;
            other.total_size_ = 0;
        }
        return *this;
    }

    template <typename... Args>
    T& emplace_back(Args&&... args) {
        if (chunks_.empty() || current_chunk_count_ == ChunkSize) {
            allocate_chunk();
        }
        auto* ptr = reinterpret_cast<T*>(&chunks_.back()->storage[current_chunk_count_ * sizeof(T)]);
        ::new (static_cast<void*>(ptr)) T(std::forward<Args>(args)...);
        ++current_chunk_count_;
        ++total_size_;
        return *ptr;
    }

    void push_back(T&& value) {
        emplace_back(std::move(value));
    }

    void push_back(const T& value) {
        emplace_back(value);
    }

    [[nodiscard]] size_t size() const noexcept {
        return total_size_;
    }

    [[nodiscard]] bool empty() const noexcept {
        return total_size_ == 0;
    }

    std::vector<T> to_vector() {
        std::vector<T> result;
        result.reserve(total_size_);
        for (size_t c = 0; c < chunks_.size(); ++c) {
            size_t count_in_chunk = (c + 1 == chunks_.size()) ? current_chunk_count_ : ChunkSize;
            auto* chunk_data = reinterpret_cast<T*>(&chunks_[c]->storage[0]);
            for (size_t i = 0; i < count_in_chunk; ++i) {
                result.emplace_back(std::move(chunk_data[i]));
            }
        }
        return result;
    }

    void clear() noexcept {
        for (size_t c = 0; c < chunks_.size(); ++c) {
            size_t count_in_chunk = (c + 1 == chunks_.size()) ? current_chunk_count_ : ChunkSize;
            auto* chunk_data = reinterpret_cast<T*>(&chunks_[c]->storage[0]);
            for (size_t i = 0; i < count_in_chunk; ++i) {
                chunk_data[i].~T();
            }
        }
        chunks_.clear();
        current_chunk_count_ = 0;
        total_size_ = 0;
    }

private:
    struct Chunk {
        alignas(alignof(T)) std::byte storage[ChunkSize * sizeof(T)];
    };

    std::vector<std::unique_ptr<Chunk>> chunks_;
    size_t current_chunk_count_ = 0;
    size_t total_size_ = 0;

    void allocate_chunk() {
        chunks_.push_back(std::make_unique<Chunk>());
        current_chunk_count_ = 0;
    }
};

} // namespace cpplinq
