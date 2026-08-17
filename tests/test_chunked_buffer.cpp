#include <gtest/gtest.h>
#include "cpplinq/core/chunked_buffer.h"
#include <string>
#include <memory>
#include <vector>

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

TEST(ChunkedBufferTest, ExactlyOneChunk) {
    ChunkedBuffer<int, 4> buffer;
    for (int i = 0; i < 4; ++i) {
        buffer.push_back(i * 100);
    }
    EXPECT_EQ(buffer.size(), 4u);

    auto vec = buffer.to_vector();
    ASSERT_EQ(vec.size(), 4u);
    for (int i = 0; i < 4; ++i) {
        EXPECT_EQ(vec[i], i * 100);
    }
}

TEST(ChunkedBufferTest, MultipleChunksSpanning) {
    ChunkedBuffer<int, 4> buffer;
    constexpr int total = 15; // 4 + 4 + 4 + 3
    for (int i = 0; i < total; ++i) {
        buffer.emplace_back(i);
    }
    EXPECT_EQ(buffer.size(), static_cast<size_t>(total));

    auto vec = buffer.to_vector();
    ASSERT_EQ(vec.size(), static_cast<size_t>(total));
    for (int i = 0; i < total; ++i) {
        EXPECT_EQ(vec[i], i);
    }
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
        // Clear explicitly
        buffer.clear();
        EXPECT_EQ(DestructorTracker::destruct_count, 10);
    }
    // No double destruction on scope exit
    EXPECT_EQ(DestructorTracker::destruct_count, 10);
}

TEST(ChunkedBufferTest, MoveConstructorAndAssignment) {
    ChunkedBuffer<std::string, 4> buffer1;
    buffer1.emplace_back("hello");
    buffer1.emplace_back("world");

    ChunkedBuffer<std::string, 4> buffer2 = std::move(buffer1);
    EXPECT_EQ(buffer1.size(), 0u);
    EXPECT_EQ(buffer2.size(), 2u);

    auto vec = buffer2.to_vector();
    ASSERT_EQ(vec.size(), 2u);
    EXPECT_EQ(vec[0], "hello");
    EXPECT_EQ(vec[1], "world");
}
