#include <gtest/gtest.h>

// Proves the toolchain works end to end: compiler, CMake, GoogleTest, ctest.
// Delete it once your Phase 1 tests exist.
TEST(Smoke, ToolchainWorks) {
    EXPECT_EQ(1 + 1, 2);
}
