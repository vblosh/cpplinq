#pragma once

#include <vector>
#include <memory>
#include <utility>
#include <cstddef>
#include <new>
#include <type_traits>
#include <iterator>
#include <stdexcept>
#include <compare>

namespace cpplinq {

template <typename T, size_t ChunkSize>
class ChunkedBuffer;

// ============================================================================
// ChunkedBufferIterator (Random Access Iterator)
// ============================================================================

template <typename T, size_t ChunkSize, bool IsConst>
class ChunkedBufferIterator {
public:
    using iterator_category = std::random_access_iterator_tag;
    using iterator_concept  = std::random_access_iterator_tag;
    using value_type        = T;
    using difference_type   = std::ptrdiff_t;
    using pointer           = std::conditional_t<IsConst, const T*, T*>;
    using reference         = std::conditional_t<IsConst, const T&, T&>;
    using ContainerType     = std::conditional_t<IsConst, const ChunkedBuffer<T, ChunkSize>, ChunkedBuffer<T, ChunkSize>>;

    ChunkedBufferIterator() noexcept : container_(nullptr), index_(0) {}

    ChunkedBufferIterator(ContainerType* container, size_t index) noexcept
        : container_(container), index_(index) {}

    // Non-const to const iterator conversion
    template <bool OtherConst, typename = std::enable_if_t<IsConst && !OtherConst>>
    ChunkedBufferIterator(const ChunkedBufferIterator<T, ChunkSize, OtherConst>& other) noexcept
        : container_(other.container_), index_(other.index_) {}

    reference operator*() const noexcept {
        return (*container_)[index_];
    }

    pointer operator->() const noexcept {
        return &((*container_)[index_]);
    }

    reference operator[](difference_type n) const noexcept {
        return (*container_)[static_cast<size_t>(static_cast<difference_type>(index_) + n)];
    }

    ChunkedBufferIterator& operator++() noexcept {
        ++index_;
        return *this;
    }

    ChunkedBufferIterator operator++(int) noexcept {
        auto tmp = *this;
        ++index_;
        return tmp;
    }

    ChunkedBufferIterator& operator--() noexcept {
        --index_;
        return *this;
    }

    ChunkedBufferIterator operator--(int) noexcept {
        auto tmp = *this;
        --index_;
        return tmp;
    }

    ChunkedBufferIterator& operator+=(difference_type n) noexcept {
        index_ = static_cast<size_t>(static_cast<difference_type>(index_) + n);
        return *this;
    }

    ChunkedBufferIterator& operator-=(difference_type n) noexcept {
        index_ = static_cast<size_t>(static_cast<difference_type>(index_) - n);
        return *this;
    }

    friend ChunkedBufferIterator operator+(ChunkedBufferIterator it, difference_type n) noexcept {
        it += n;
        return it;
    }

    friend ChunkedBufferIterator operator+(difference_type n, ChunkedBufferIterator it) noexcept {
        it += n;
        return it;
    }

    friend ChunkedBufferIterator operator-(ChunkedBufferIterator it, difference_type n) noexcept {
        it -= n;
        return it;
    }

    friend difference_type operator-(const ChunkedBufferIterator& lhs, const ChunkedBufferIterator& rhs) noexcept {
        return static_cast<difference_type>(lhs.index_) - static_cast<difference_type>(rhs.index_);
    }

    friend bool operator==(const ChunkedBufferIterator& lhs, const ChunkedBufferIterator& rhs) noexcept {
        return lhs.index_ == rhs.index_ && lhs.container_ == rhs.container_;
    }

    friend auto operator<=>(const ChunkedBufferIterator& lhs, const ChunkedBufferIterator& rhs) noexcept {
        return lhs.index_ <=> rhs.index_;
    }

private:
    template <typename, size_t, bool>
    friend class ChunkedBufferIterator;

    ContainerType* container_ = nullptr;
    size_t index_ = 0;
};

// ============================================================================
// ChunkedBuffer / ChunkedList
// ============================================================================

template <typename T, size_t ChunkSize = 64>
class ChunkedBuffer {
public:
    using value_type             = T;
    using size_type              = size_t;
    using difference_type        = std::ptrdiff_t;
    using reference              = T&;
    using const_reference        = const T&;
    using pointer                = T*;
    using const_pointer          = const T*;
    using iterator               = ChunkedBufferIterator<T, ChunkSize, false>;
    using const_iterator         = ChunkedBufferIterator<T, ChunkSize, true>;
    using reverse_iterator       = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

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

    // Element Access
    reference operator[](size_t index) noexcept {
        size_t chunk_idx = index / ChunkSize;
        size_t elem_idx = index % ChunkSize;
        return *reinterpret_cast<T*>(&chunks_[chunk_idx]->storage[elem_idx * sizeof(T)]);
    }

    const_reference operator[](size_t index) const noexcept {
        size_t chunk_idx = index / ChunkSize;
        size_t elem_idx = index % ChunkSize;
        return *reinterpret_cast<const T*>(&chunks_[chunk_idx]->storage[elem_idx * sizeof(T)]);
    }

    reference at(size_t index) {
        if (index >= total_size_) {
            throw std::out_of_range("ChunkedBuffer::at: index out of range");
        }
        return (*this)[index];
    }

    const_reference at(size_t index) const {
        if (index >= total_size_) {
            throw std::out_of_range("ChunkedBuffer::at: index out of range");
        }
        return (*this)[index];
    }

    reference front() {
        return at(0);
    }

    const_reference front() const {
        return at(0);
    }

    reference back() {
        if (total_size_ == 0) {
            throw std::out_of_range("ChunkedBuffer::back: empty container");
        }
        return (*this)[total_size_ - 1];
    }

    const_reference back() const {
        if (total_size_ == 0) {
            throw std::out_of_range("ChunkedBuffer::back: empty container");
        }
        return (*this)[total_size_ - 1];
    }

    // Iterators (Forward, Bidirectional, Random Access)
    iterator begin() noexcept {
        return iterator(this, 0);
    }

    iterator end() noexcept {
        return iterator(this, total_size_);
    }

    const_iterator begin() const noexcept {
        return const_iterator(this, 0);
    }

    const_iterator end() const noexcept {
        return const_iterator(this, total_size_);
    }

    const_iterator cbegin() const noexcept {
        return const_iterator(this, 0);
    }

    const_iterator cend() const noexcept {
        return const_iterator(this, total_size_);
    }

    // Reverse Iterators
    reverse_iterator rbegin() noexcept {
        return reverse_iterator(end());
    }

    reverse_iterator rend() noexcept {
        return reverse_iterator(begin());
    }

    const_reverse_iterator rbegin() const noexcept {
        return const_reverse_iterator(end());
    }

    const_reverse_iterator rend() const noexcept {
        return const_reverse_iterator(begin());
    }

    const_reverse_iterator crbegin() const noexcept {
        return const_reverse_iterator(cend());
    }

    const_reverse_iterator crend() const noexcept {
        return const_reverse_iterator(cbegin());
    }

    // Conversion to contiguous std::vector
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

// Convenient aliases
template <typename T, size_t ChunkSize = 64>
using ChunkedList = ChunkedBuffer<T, ChunkSize>;

template <typename T, size_t ChunkSize = 64>
using chunked_list = ChunkedBuffer<T, ChunkSize>;

} // namespace cpplinq
