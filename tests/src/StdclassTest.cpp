#include "gtest/gtest.h"
#include "stdclass.h"

class StdclassTest : public ::testing::Test
{
};

TEST_F(StdclassTest, join_paths_left_empty)
{
    // Arrange
    const std::string left = "";
    const std::string right = "right";

    // Act
    const std::string joined = join_paths(left, right);

    // Assert
    ASSERT_EQ(joined, right);
}

#ifdef _WIN32

TEST_F(StdclassTest, join_paths_win32_nominal)
{
    // Arrange
    const std::string left = R"(C:\Users\theusr//)";
    const std::string right = R"(\\right/path)";

    // Act
    const std::string joined = join_paths(left, right);

    // Assert
    ASSERT_EQ(joined, R"(C:\Users\theusr\right/path)");
}

TEST_F(StdclassTest, join_paths_win32_left_is_only_slashes)
{
    // Arrange
    const std::string left = R"(\\)";
    const std::string right = R"(/right/path)";

    // Act
    const std::string joined = join_paths(left, right);

    // Assert
    ASSERT_EQ(joined, R"(\\right/path)");
}

TEST_F(StdclassTest, fix_path_win32_nominal)
{
    // Arrange
    const std::string path = R"(C:\Users/theusr//\abc\123/987)";

    // Act
    const std::string fixedPath = fix_path(path);

    // Assert
    ASSERT_EQ(fixedPath, R"(C:\Users\theusr\abc\123\987)");
}

TEST_F(StdclassTest, fix_path_win32_with_leading_double_slash)
{
    // Arrange
    const std::string path = R"(\\wsl.localhost//\\\/Ubuntu-22.04\)";

    // Act
    const std::string fixedPath = fix_path(path);

    // Assert
    ASSERT_EQ(fixedPath, R"(\\wsl.localhost\Ubuntu-22.04\)");
}

#else // not _WIN32

TEST_F(StdclassTest, join_paths_nominal)
{
    // Arrange
    const std::string left = "/home/theusr//";
    const std::string right = "/right/path";

    // Act
    const std::string joined = join_paths(left, right);

    // Assert
    ASSERT_EQ(joined, "/home/theusr/right/path");
}

TEST_F(StdclassTest, join_paths_left_is_only_slashes)
{
    // Arrange
    const std::string left = "//";
    const std::string right = "/right/path";

    // Act
    const std::string joined = join_paths(left, right);

    // Assert
    ASSERT_EQ(joined, "//right/path");
}

TEST_F(StdclassTest, fix_path_nominal)
{
    // Arrange
    const std::string path = "/home/theusr////abc/123//987";

    // Act
    const std::string fixedPath = fix_path(path);

    // Assert
    ASSERT_EQ(fixedPath, "/home/theusr/abc/123/987");
}

TEST_F(StdclassTest, fix_path_with_leading_double_slash)
{
    // Arrange
    const std::string path = "//home/theusr/";

    // Act
    const std::string fixedPath = fix_path(path);

    // Assert
    ASSERT_EQ(fixedPath, "/home/theusr/");
}

#endif
