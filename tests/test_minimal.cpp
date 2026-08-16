#include <gtest/gtest.h>
#include <iostream>

TEST(SampleTest, TrueIsTrue) {
    EXPECT_TRUE(true);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    std::cout << "Inside main: total test suites = "
              << ::testing::UnitTest::GetInstance()->total_test_suite_count()
              << ", total tests = "
              << ::testing::UnitTest::GetInstance()->total_test_count()
              << std::endl;
    return RUN_ALL_TESTS();
}
