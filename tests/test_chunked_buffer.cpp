#include <gtest/gtest.h>
#include "cpplinq/core/chunked_buffer.h"
#include <string>
#include <memory>
#include <vector>
#include <algorithm>
#include <numeric>
#include <ranges>

using namespace cpplinq;

namespace {

struct DestructorTracker {
    static int construct_count;
    static int destruct_count;
    static int move_count;

    int value = 0;

    static void reset() {
        construct_count = 0;
        destruct_count = 0;
        move_count = 0;
    }

    DestructorTracker(int v = 0) : value(v) {
        ++construct_count;
    }

    ~DestructorTracker() {
        ++destruct_count;
    }

    DestructorTracker(const DestructorTracker& other) : value(other.value) {
        ++construct_count;
    }

    DestructorTracker& operator=(const DestructorTracker& other) {
        value = other.value;
        return *this;
    }

    DestructorTracker(DestructorTracker&& other) noexcept : value(other.value) {
        ++move_count;
        other.value = -1;
    }

    DestructorTracker& operator=(DestructorTracker&& other) noexcept {
        value = other.value;
        other.value = -1;
        return *this;
    }
};

int DestructorTracker::construct_count = 0;
int DestructorTracker::destruct_count = 0;
int DestructorTracker::move_count = 0;

} // namespace

TEST(ChunkedBufferTest, EmptyBuffer) {
    ChunkedBuffer<int, 4> buffer;
    EXPECT_TRUE(buffer.empty());
    EXPECT_EQ(buffer.size(), 0u);
    EXPECT_EQ(buffer.begin(), buffer.end());

    auto vec = buffer.to_vector();
    EXPECT_TRUE(vec.empty());
    EXPECT_EQ(vec.size(), 0u);
}

TEST(ChunkedBufferTest, SingleChunkSubCapacity) {
    ChunkedBuffer<int, 8> buffer;
    buffer.push_back(10);
    buffer.push_back(20);
    buffer.push_back(30);

    EXPECT_FALSE(buffer.empty());
    EXPECT_EQ(buffer.size(), 3u);

    auto vec = buffer.to_vector();
    ASSERT_EQ(vec.size(), 3u);
    EXPECT_EQ(vec[0], 10);
    EXPECT_EQ(vec[1], 20);
    EXPECT_EQ(vec[2], 30);
}

TEST(ChunkedBufferTest, ElementAccessAndAt) {
    ChunkedBuffer<int, 4> buffer;
    for (int i = 0; i < 10; ++i) {
        buffer.push_back(i * 10);
    }
    EXPECT_EQ(buffer.front(), 0);
    EXPECT_EQ(buffer.back(), 90);
    EXPECT_EQ(buffer[5], 50);
    EXPECT_EQ(buffer.at(5), 50);
    EXPECT_THROW(buffer.at(10), std::out_of_range);
}

TEST(ChunkedBufferTest, ForwardIteration) {
    ChunkedList<int, 4> list;
    for (int i = 0; i < 10; ++i) {
        list.push_back(i + 1);
    }

    int expected = 1;
    for (int val : list) {
        EXPECT_EQ(val, expected++);
    }
    EXPECT_EQ(expected, 11);
}

TEST(ChunkedBufferTest, ReverseIteration) {
    ChunkedBuffer<int, 4> buffer;
    for (int i = 0; i < 10; ++i) {
        buffer.push_back(i);
    }

    std::vector<int> reversed;
    for (auto it = buffer.rbegin(); it != buffer.rend(); ++it) {
        reversed.push_back(*it);
    }

    ASSERT_EQ(reversed.size(), 10u);
    for (int i = 0; i < 10; ++i) {
        EXPECT_EQ(reversed[i], 9 - i);
    }
}

TEST(ChunkedBufferTest, RandomAccessArithmetic) {
    ChunkedBuffer<int, 4> buffer;
    for (int i = 0; i < 20; ++i) {
        buffer.push_back(i * 5);
    }

    auto it = buffer.begin();
    EXPECT_EQ(*it, 0);
    EXPECT_EQ(*(it + 4), 20);
    EXPECT_EQ(*(it + 7), 35);
    EXPECT_EQ(it[10], 50);

    auto it2 = it + 15;
    EXPECT_EQ(*it2, 75);
    EXPECT_EQ(it2 - it, 15);
    EXPECT_EQ(it - it2, -15);

    it2 -= 5;
    EXPECT_EQ(*it2, 50);
    EXPECT_TRUE(it < it2);
    EXPECT_TRUE(it2 > it);
    EXPECT_TRUE(it <= it2);
    EXPECT_TRUE(it2 >= it);
}

TEST(ChunkedBufferTest, StandardAlgorithmsSortAndFind) {
    ChunkedBuffer<int, 4> buffer;
    std::vector<int> raw = {42, 17, 99, 8, 23, 54, 3, 76, 12, 65, 88, 31};
    for (int v : raw) {
        buffer.push_back(v);
    }

    // std::sort with random access iterators across chunks
    std::sort(buffer.begin(), buffer.end());

    EXPECT_TRUE(std::is_sorted(buffer.begin(), buffer.end()));
    EXPECT_EQ(buffer.front(), 3);
    EXPECT_EQ(buffer.back(), 99);

    // std::binary_search
    EXPECT_TRUE(std::binary_search(buffer.begin(), buffer.end(), 54));
    EXPECT_FALSE(std::binary_search(buffer.begin(), buffer.end(), 100));

    // std::find
    auto found = std::find(buffer.begin(), buffer.end(), 42);
    ASSERT_NE(found, buffer.end());
    EXPECT_EQ(*found, 42);
}

TEST(ChunkedBufferTest, Cpp20RangesCompatibility) {
    ChunkedBuffer<int, 4> buffer;
    for (int i = 1; i <= 20; ++i) {
        buffer.push_back(i);
    }

    // C++20 filter and transform view
    auto even_squares = buffer 
        | std::views::filter([](int n) { return n % 2 == 0; })
        | std::views::transform([](int n) { return n * n; });

    std::vector<int> results;
    for (int val : even_squares) {
        results.push_back(val);
    }

    ASSERT_EQ(results.size(), 10u);
    EXPECT_EQ(results[0], 4);   // 2^2
    EXPECT_EQ(results[1], 16);  // 4^2
    EXPECT_EQ(results[9], 400); // 20^2
}

TEST(ChunkedBufferTest, ConstIterators) {
    ChunkedBuffer<std::string, 4> buffer;
    buffer.emplace_back("alpha");
    buffer.emplace_back("beta");
    buffer.emplace_back("gamma");

    const auto& const_buf = buffer;
    auto cit = const_buf.cbegin();
    EXPECT_EQ(*cit, "alpha");
    ++cit;
    EXPECT_EQ(*cit, "beta");
    EXPECT_EQ(cit->length(), 4u);
}

TEST(ChunkedBufferTest, LargeScaleElements) {
    ChunkedBuffer<std::string, 64> buffer;
    constexpr int count = 1000;
    for (int i = 0; i < count; ++i) {
        buffer.emplace_back("item_" + std::to_string(i));
    }
    EXPECT_EQ(buffer.size(), static_cast<size_t>(count));

    auto vec = buffer.to_vector();
    ASSERT_EQ(vec.size(), static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        EXPECT_EQ(vec[i], "item_" + std::to_string(i));
    }
}

TEST(ChunkedBufferTest, MoveOnlyTypes) {
    ChunkedBuffer<std::unique_ptr<int>, 4> buffer;
    for (int i = 0; i < 10; ++i) {
        buffer.push_back(std::make_unique<int>(i * 7));
    }
    EXPECT_EQ(buffer.size(), 10u);

    auto vec = std::move(buffer).to_vector();
    ASSERT_EQ(vec.size(), 10u);
    for (int i = 0; i < 10; ++i) {
        ASSERT_NE(vec[i], nullptr);
        EXPECT_EQ(*vec[i], i * 7);
    }
}

TEST(ChunkedBufferTest, ProperDestructionOnClear) {
    DestructorTracker::reset();
    {
        ChunkedBuffer<DestructorTracker, 4> buffer;
        for (int i = 0; i < 10; ++i) {
            buffer.emplace_back(i);
        }
        EXPECT_EQ(DestructorTracker::construct_count, 10);
        buffer.clear();
        EXPECT_EQ(DestructorTracker::destruct_count, 10);
    }
    EXPECT_EQ(DestructorTracker::destruct_count, 10);
}

TEST(ChunkedBufferTest, ZeroMoveConstructionAndValueReturn) {
    DestructorTracker::reset();
    auto make_list = []() -> ChunkedList<DestructorTracker, 4> {
        ChunkedList<DestructorTracker, 4> list;
        for (int i = 0; i < 10; ++i) {
            auto& elem = list.emplace_back();
            elem.value = i;
        }
        return list; // Return ChunkedList by value (O(1) pointer transfer)
    };

    auto list = make_list();
    EXPECT_EQ(list.size(), 10u);
    EXPECT_EQ(DestructorTracker::construct_count, 10);
    EXPECT_EQ(DestructorTracker::move_count, 0); // Elements inside chunks are never moved!

    for (int i = 0; i < 10; ++i) {
        EXPECT_EQ(list[i].value, i);
    }
}

TEST(ChunkedBufferTest, ForEachChunkIteration) {
    ChunkedBuffer<int, 4> buffer;
    for (int i = 0; i < 10; ++i) {
        buffer.push_back(i + 1);
    }

    std::vector<int> collected;
    buffer.for_each_chunk([&](const int* chunk_data, size_t count) {
        for (size_t i = 0; i < count; ++i) {
            collected.push_back(chunk_data[i]);
        }
    });

    ASSERT_EQ(collected.size(), 10u);
    for (int i = 0; i < 10; ++i) {
        EXPECT_EQ(collected[i], i + 1);
    }
}

TEST(ChunkedBufferTest, RvalueToVectorClearsBuffer) {
    ChunkedBuffer<std::string, 4> buffer;
    buffer.emplace_back("hello");
    buffer.emplace_back("world");

    auto vec = std::move(buffer).to_vector();
    ASSERT_EQ(vec.size(), 2u);
    EXPECT_EQ(vec[0], "hello");
    EXPECT_EQ(vec[1], "world");
    EXPECT_EQ(buffer.size(), 0u);
    EXPECT_TRUE(buffer.empty());
}

TEST(ChunkedBufferTest, LvalueToVectorCopiesElementsAndPreservesBuffer) {
    DestructorTracker::reset();

    ChunkedBuffer<DestructorTracker, 4> buffer;
    for (int i = 0; i < 5; ++i) {
        buffer.emplace_back(i);
    }
    ASSERT_EQ(buffer.size(), 5u);
    ASSERT_EQ(DestructorTracker::construct_count, 5);

    auto vec = buffer.to_vector();

    ASSERT_EQ(vec.size(), 5u);
    EXPECT_EQ(buffer.size(), 5u);
    EXPECT_EQ(DestructorTracker::construct_count, 10); // five copies into the vector
    EXPECT_EQ(DestructorTracker::move_count, 0);
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(vec[static_cast<size_t>(i)].value, i);
        EXPECT_EQ(buffer[static_cast<size_t>(i)].value, i);
    }
}

TEST(ChunkedBufferTest, RvalueToVectorMovesElementsAndClearsBuffer) {
    DestructorTracker::reset();

    ChunkedBuffer<DestructorTracker, 4> buffer;
    for (int i = 0; i < 5; ++i) {
        buffer.emplace_back(i);
    }
    ASSERT_EQ(buffer.size(), 5u);

    auto vec = std::move(buffer).to_vector();

    ASSERT_EQ(vec.size(), 5u);
    EXPECT_TRUE(buffer.empty());
    EXPECT_EQ(DestructorTracker::construct_count, 5); // only the original emplacements
    EXPECT_EQ(DestructorTracker::move_count, 5);
    EXPECT_EQ(DestructorTracker::destruct_count, 5); // source elements destroyed by clear()
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(vec[static_cast<size_t>(i)].value, i);
    }
}

TEST(ChunkedBufferTest, IteratorComparisonDifferentContainers) {
    ChunkedBuffer<int, 4> buf1;
    ChunkedBuffer<int, 4> buf2;
    buf1.push_back(1);
    buf2.push_back(1);

    auto it1 = buf1.begin();
    auto it2 = buf2.begin();

    EXPECT_NE(it1, it2);
    EXPECT_FALSE(it1 == it2);
    EXPECT_TRUE(it1 != it2);
    // Comparison across different containers compares container pointers
    if (&buf1 < &buf2) {
        EXPECT_TRUE(it1 < it2);
        EXPECT_FALSE(it1 > it2);
    } else {
        EXPECT_TRUE(it1 > it2);
        EXPECT_FALSE(it1 < it2);
    }
}

TEST(ChunkedBufferTest, EmplaceBackReturnsReference) {
    ChunkedBuffer<std::string, 4> buffer;
    auto& ref1 = buffer.emplace_back("first");
    EXPECT_EQ(ref1, "first");
    ref1 = "modified_first";
    EXPECT_EQ(buffer[0], "modified_first");

    auto& ref2 = buffer.emplace_back("second");
    EXPECT_EQ(ref2, "second");
    EXPECT_EQ(buffer.size(), 2u);
}

namespace {
struct ThrowingType {
    static inline int live_instances = 0;
    static inline bool should_throw = false;

    ThrowingType() {
        if (should_throw) throw std::runtime_error("Constructor threw");
        ++live_instances;
    }
    ~ThrowingType() {
        --live_instances;
    }
};
} // namespace

TEST(ChunkedBufferTest, EmptyChunkAfterConstructorThrowDoesNotLaunder) {
    ThrowingType::live_instances = 0;
    ThrowingType::should_throw = false;

    ChunkedBuffer<ThrowingType, 2> buffer;
    buffer.emplace_back();
    buffer.emplace_back();
    ThrowingType::should_throw = true;
    EXPECT_THROW(buffer.emplace_back(), std::runtime_error);

    EXPECT_EQ(buffer.size(), 2u);
    EXPECT_NO_THROW(buffer.clear());
    EXPECT_EQ(buffer.size(), 0u);
    EXPECT_EQ(ThrowingType::live_instances, 0);
}

namespace {
template <typename C>
concept HasReserveMethod = requires(C c) { c.reserve(10); };

template <typename C>
concept HasShrinkToFitMethod = requires(C c) { c.shrink_to_fit(); };

template <typename C>
concept HasPopBackMethod = requires(C c) { c.pop_back(); };
} // namespace

TEST(ChunkedBufferTest, MoveConstructorAndAssignment) {
    // 1. Move construct empty buffer
    {
        ChunkedBuffer<int, 4> src;
        EXPECT_TRUE(src.empty());
        ChunkedBuffer<int, 4> dst(std::move(src));
        EXPECT_TRUE(dst.empty());
        EXPECT_EQ(dst.size(), 0u);
        EXPECT_TRUE(src.empty());
        EXPECT_EQ(src.size(), 0u);
    }

    // 2. Move construct populated buffer across multiple chunk boundaries
    {
        DestructorTracker::reset();
        {
            ChunkedBuffer<DestructorTracker, 2> src;
            for (int i = 0; i < 7; ++i) {
                src.emplace_back(i * 10);
            }
            EXPECT_EQ(src.size(), 7u);
            EXPECT_EQ(DestructorTracker::construct_count, 7);
            EXPECT_EQ(DestructorTracker::move_count, 0);

            ChunkedBuffer<DestructorTracker, 2> dst(std::move(src));
            EXPECT_TRUE(src.empty());
            EXPECT_EQ(src.size(), 0u);
            EXPECT_EQ(dst.size(), 7u);
            EXPECT_EQ(DestructorTracker::construct_count, 7);
            EXPECT_EQ(DestructorTracker::move_count, 0); // No element moves, only chunk pointers transferred!

            for (int i = 0; i < 7; ++i) {
                EXPECT_EQ(dst[i].value, i * 10);
            }
        }
        EXPECT_EQ(DestructorTracker::destruct_count, 7);
    }

    // 3. Move assignment self-assignment
    {
        ChunkedBuffer<int, 4> buf;
        buf.push_back(10);
        buf.push_back(20);
        auto* p = &buf;
        *p = std::move(buf);
        EXPECT_EQ(buf.size(), 2u);
        EXPECT_EQ(buf[0], 10);
        EXPECT_EQ(buf[1], 20);
    }

    // 4. Move assignment into populated buffer (destructs previous contents)
    {
        DestructorTracker::reset();
        {
            ChunkedBuffer<DestructorTracker, 2> dst;
            dst.emplace_back(1);
            dst.emplace_back(2);
            dst.emplace_back(3);

            ChunkedBuffer<DestructorTracker, 2> src;
            src.emplace_back(100);
            src.emplace_back(200);

            EXPECT_EQ(DestructorTracker::construct_count, 5);
            EXPECT_EQ(DestructorTracker::destruct_count, 0);

            dst = std::move(src);

            EXPECT_EQ(dst.size(), 2u);
            EXPECT_EQ(dst[0].value, 100);
            EXPECT_EQ(dst[1].value, 200);
            EXPECT_TRUE(src.empty());
            EXPECT_EQ(src.size(), 0u);

            // Previous 3 elements in dst were destroyed during clear() inside operator=
            EXPECT_EQ(DestructorTracker::destruct_count, 3);
        }
        EXPECT_EQ(DestructorTracker::destruct_count, 5);
    }
}

TEST(ChunkedBufferTest, ContainerTraitsAndDeletedOperations) {
    static_assert(!std::is_copy_constructible_v<ChunkedBuffer<int, 4>>);
    static_assert(!std::is_copy_assignable_v<ChunkedBuffer<int, 4>>);
    static_assert(!std::is_copy_constructible_v<ChunkedList<int, 4>>);
    static_assert(!std::is_copy_assignable_v<ChunkedList<int, 4>>);
    static_assert(std::is_move_constructible_v<ChunkedBuffer<int, 4>>);
    static_assert(std::is_move_assignable_v<ChunkedBuffer<int, 4>>);

    // Verify ChunkedBuffer does not have reserve, shrink_to_fit, or pop_back
    static_assert(!HasReserveMethod<ChunkedBuffer<int, 4>>);
    static_assert(!HasShrinkToFitMethod<ChunkedBuffer<int, 4>>);
    static_assert(!HasPopBackMethod<ChunkedBuffer<int, 4>>);
}

TEST(ChunkedBufferTest, FrontBackEmptyAndConstAccess) {
    // Empty non-const container
    ChunkedBuffer<int, 4> buf;
    EXPECT_TRUE(buf.empty());
    EXPECT_EQ(buf.size(), 0u);
    EXPECT_THROW(buf.front(), std::out_of_range);
    EXPECT_THROW(buf.back(), std::out_of_range);

    // Empty const container
    const auto& cbuf = buf;
    EXPECT_TRUE(cbuf.empty());
    EXPECT_EQ(cbuf.size(), 0u);
    EXPECT_THROW(cbuf.front(), std::out_of_range);
    EXPECT_THROW(cbuf.back(), std::out_of_range);

    // Populated single element
    buf.push_back(42);
    EXPECT_FALSE(buf.empty());
    EXPECT_EQ(buf.size(), 1u);
    EXPECT_EQ(buf.front(), 42);
    EXPECT_EQ(buf.back(), 42);
    EXPECT_EQ(cbuf.front(), 42);
    EXPECT_EQ(cbuf.back(), 42);

    // Mutating via non-const front()
    buf.front() = 99;
    EXPECT_EQ(buf.front(), 99);
    EXPECT_EQ(buf.back(), 99);

    // Multiple elements spanning chunk boundary
    buf.push_back(100);
    buf.push_back(200);
    buf.push_back(300);
    buf.push_back(400); // 5 elements, chunks of 4 -> spans boundary
    EXPECT_EQ(buf.size(), 5u);
    EXPECT_EQ(buf.front(), 99);
    EXPECT_EQ(buf.back(), 400);
    EXPECT_EQ(cbuf.front(), 99);
    EXPECT_EQ(cbuf.back(), 400);

    // Mutating via non-const back()
    buf.back() = 555;
    EXPECT_EQ(buf.back(), 555);
    EXPECT_EQ(cbuf.back(), 555);
}

TEST(ChunkedBufferTest, PushAndEmplaceAcrossChunkBoundaries) {
    ChunkedBuffer<int, 2> buf;
    EXPECT_TRUE(buf.empty());
    EXPECT_EQ(buf.size(), 0u);

    // Push lvalue
    const int val1 = 11;
    buf.push_back(val1);
    EXPECT_FALSE(buf.empty());
    EXPECT_EQ(buf.size(), 1u);
    EXPECT_EQ(buf[0], 11);

    // Push rvalue (fills chunk 0)
    buf.push_back(22);
    EXPECT_EQ(buf.size(), 2u);
    EXPECT_EQ(buf[1], 22);

    // Emplace back (allocates chunk 1)
    buf.emplace_back(33);
    EXPECT_EQ(buf.size(), 3u);
    EXPECT_EQ(buf[2], 33);

    // Fills chunk 1
    buf.push_back(44);
    EXPECT_EQ(buf.size(), 4u);
    EXPECT_EQ(buf[3], 44);

    // Allocates chunk 2
    buf.push_back(55);
    EXPECT_EQ(buf.size(), 5u);
    EXPECT_EQ(buf[4], 55);

    // Verify all indexing and at()
    for (size_t i = 0; i < buf.size(); ++i) {
        EXPECT_EQ(buf[i], static_cast<int>((i + 1) * 11));
        EXPECT_EQ(buf.at(i), static_cast<int>((i + 1) * 11));
    }

    const auto& cbuf = buf;
    for (size_t i = 0; i < cbuf.size(); ++i) {
        EXPECT_EQ(cbuf[i], static_cast<int>((i + 1) * 11));
        EXPECT_EQ(cbuf.at(i), static_cast<int>((i + 1) * 11));
    }
}

TEST(ChunkedBufferTest, ClearEdgeCasesAndReuse) {
    DestructorTracker::reset();
    ChunkedBuffer<DestructorTracker, 2> buf;

    // Clear on empty buffer
    EXPECT_NO_THROW(buf.clear());
    EXPECT_TRUE(buf.empty());
    EXPECT_EQ(buf.size(), 0u);

    // Push elements
    buf.emplace_back(1);
    buf.emplace_back(2);
    buf.emplace_back(3);
    EXPECT_EQ(buf.size(), 3u);
    EXPECT_EQ(DestructorTracker::construct_count, 3);
    EXPECT_EQ(DestructorTracker::destruct_count, 0);

    // Clear populated
    buf.clear();
    EXPECT_TRUE(buf.empty());
    EXPECT_EQ(buf.size(), 0u);
    EXPECT_EQ(DestructorTracker::destruct_count, 3);

    // Clear again
    buf.clear();
    EXPECT_TRUE(buf.empty());
    EXPECT_EQ(buf.size(), 0u);
    EXPECT_EQ(DestructorTracker::destruct_count, 3);

    // Push after clear
    buf.emplace_back(10);
    buf.emplace_back(20);
    EXPECT_EQ(buf.size(), 2u);
    EXPECT_EQ(buf[0].value, 10);
    EXPECT_EQ(buf[1].value, 20);
}

TEST(ChunkedBufferTest, AtOutOfBoundsConstAndNonConst) {
    ChunkedBuffer<std::string, 2> buf;
    EXPECT_THROW(buf.at(0), std::out_of_range);

    const auto& cbuf = buf;
    EXPECT_THROW(cbuf.at(0), std::out_of_range);

    buf.push_back("a");
    buf.push_back("b");
    EXPECT_EQ(buf.at(0), "a");
    EXPECT_EQ(buf.at(1), "b");
    EXPECT_EQ(cbuf.at(0), "a");
    EXPECT_EQ(cbuf.at(1), "b");

    EXPECT_THROW(buf.at(2), std::out_of_range);
    EXPECT_THROW(buf.at(99), std::out_of_range);
    EXPECT_THROW(cbuf.at(2), std::out_of_range);
    EXPECT_THROW(cbuf.at(99), std::out_of_range);
}

TEST(ChunkedBufferTest, IteratorComprehensiveOperations) {
    // 1. Default constructor
    ChunkedBuffer<int, 4>::iterator def_it1, def_it2;
    EXPECT_TRUE(def_it1 == def_it2);
    EXPECT_FALSE(def_it1 != def_it2);

    ChunkedBuffer<int, 4>::const_iterator def_cit1, def_cit2;
    EXPECT_TRUE(def_cit1 == def_cit2);
    EXPECT_FALSE(def_cit1 != def_cit2);

    // 2. Iterators on empty buffer
    ChunkedBuffer<int, 4> empty_buf;
    EXPECT_EQ(empty_buf.begin(), empty_buf.end());
    EXPECT_EQ(empty_buf.cbegin(), empty_buf.cend());

    // 3. Increment, decrement, and compound arithmetic across chunk boundaries
    ChunkedBuffer<int, 2> buf;
    for (int i = 0; i < 7; ++i) {
        buf.push_back(i * 10);
    }

    auto it = buf.begin();
    EXPECT_EQ(*it, 0);

    // Post-increment
    auto old_it = it++;
    EXPECT_EQ(*old_it, 0);
    EXPECT_EQ(*it, 10);

    // Pre-increment
    auto pre_it = ++it;
    EXPECT_EQ(*pre_it, 20);
    EXPECT_EQ(*it, 20);

    // Pre-decrement
    auto it_end = buf.end();
    auto pre_dec = --it_end;
    EXPECT_EQ(*pre_dec, 60);

    // Post-decrement
    auto post_dec = it_end--;
    EXPECT_EQ(*post_dec, 60);
    EXPECT_EQ(*it_end, 50);

    // Operator += and -= with positive and negative differences
    it_end += 1;
    EXPECT_EQ(*it_end, 60);

    it_end += -2;
    EXPECT_EQ(*it_end, 40);

    it_end -= 1;
    EXPECT_EQ(*it_end, 30);

    it_end -= -2;
    EXPECT_EQ(*it_end, 50);

    // Operator it + n and n + it
    auto it_add1 = buf.begin() + 4;
    EXPECT_EQ(*it_add1, 40);

    auto it_add2 = 4 + buf.begin();
    EXPECT_EQ(*it_add2, 40);

    // Operator it - n
    auto it_sub = buf.end() - 2;
    EXPECT_EQ(*it_sub, 50);

    // Operator it1 - it2
    EXPECT_EQ(buf.end() - buf.begin(), 7);
    EXPECT_EQ(buf.begin() - buf.end(), -7);

    // Operator[]
    auto mid = buf.begin() + 3;
    EXPECT_EQ(mid[0], 30);
    EXPECT_EQ(mid[2], 50);
    EXPECT_EQ(mid[-2], 10);

    // Non-const to const iterator conversion
    ChunkedBuffer<int, 2>::const_iterator cit = buf.begin();
    EXPECT_EQ(cit, buf.cbegin());
    EXPECT_EQ(*cit, 0);
    cit += 5;
    EXPECT_EQ(*cit, 50);

    // Comparisons
    auto it_a = buf.begin();
    auto it_b = buf.begin() + 3;
    EXPECT_TRUE(it_a < it_b);
    EXPECT_TRUE(it_a <= it_b);
    EXPECT_TRUE(it_b > it_a);
    EXPECT_TRUE(it_b >= it_a);
    EXPECT_TRUE(it_a != it_b);
    EXPECT_FALSE(it_a == it_b);
    EXPECT_TRUE((it_a <=> it_b) < 0);
    EXPECT_TRUE((it_b <=> it_a) > 0);
    EXPECT_TRUE((it_a <=> it_a) == 0);

    // Arrow operator
    ChunkedBuffer<std::string, 2> sbuf;
    sbuf.push_back("hello");
    sbuf.push_back("world");
    EXPECT_EQ(sbuf.begin()->length(), 5u);
    EXPECT_EQ(sbuf.cbegin()->length(), 5u);
}

TEST(ChunkedBufferTest, ReverseIteratorsAndConstOverloads) {
    // Empty container reverse iterators
    ChunkedBuffer<int, 4> empty_buf;
    EXPECT_EQ(empty_buf.rbegin(), empty_buf.rend());
    EXPECT_EQ(empty_buf.crbegin(), empty_buf.crend());

    const auto& c_empty = empty_buf;
    EXPECT_EQ(c_empty.rbegin(), c_empty.rend());
    EXPECT_EQ(c_empty.crbegin(), c_empty.crend());

    // Populated container
    ChunkedBuffer<int, 2> buf;
    for (int i = 0; i < 5; ++i) {
        buf.push_back(i);
    }

    std::vector<int> rev;
    for (auto it = buf.crbegin(); it != buf.crend(); ++it) {
        rev.push_back(*it);
    }
    EXPECT_EQ(rev, (std::vector<int>{4, 3, 2, 1, 0}));

    // Mutating via non-const reverse iterator
    *buf.rbegin() = 999;
    EXPECT_EQ(buf.back(), 999);

    const auto& cbuf = buf;
    EXPECT_EQ(*cbuf.rbegin(), 999);
}

TEST(ChunkedBufferTest, ForEachChunkAndToVectorEdgeCases) {
    // 1. for_each_chunk on empty container
    ChunkedBuffer<int, 4> empty_buf;
    size_t count_invocations = 0;
    empty_buf.for_each_chunk([&](int*, size_t) { ++count_invocations; });
    EXPECT_EQ(count_invocations, 0u);

    const auto& c_empty = empty_buf;
    c_empty.for_each_chunk([&](const int*, size_t) { ++count_invocations; });
    EXPECT_EQ(count_invocations, 0u);

    // 2. to_vector on empty container
    auto v_empty_const = c_empty.to_vector();
    EXPECT_TRUE(v_empty_const.empty());

    auto v_empty_rvalue = std::move(empty_buf).to_vector();
    EXPECT_TRUE(v_empty_rvalue.empty());

    // 3. for_each_chunk on multi-chunk with mutating callback
    ChunkedBuffer<int, 3> buf;
    for (int i = 0; i < 8; ++i) {
        buf.push_back(i + 1);
    }

    buf.for_each_chunk([](int* data, size_t count) {
        for (size_t i = 0; i < count; ++i) {
            data[i] *= 10;
        }
    });

    for (int i = 0; i < 8; ++i) {
        EXPECT_EQ(buf[i], (i + 1) * 10);
    }

    // 4. for_each_chunk on const multi-chunk
    const auto& cbuf = buf;
    std::vector<int> collected;
    cbuf.for_each_chunk([&](const int* data, size_t count) {
        for (size_t i = 0; i < count; ++i) {
            collected.push_back(data[i]);
        }
    });
    EXPECT_EQ(collected.size(), 8u);
    for (int i = 0; i < 8; ++i) {
        EXPECT_EQ(collected[i], (i + 1) * 10);
    }
}

