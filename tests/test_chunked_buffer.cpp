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

    auto vec = buffer.to_vector();
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
