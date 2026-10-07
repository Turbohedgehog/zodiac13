/*
 * Copyright 2026 Ivan Kulenko / Zodiac13
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://apache.org
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <string_view>

#include <lib_core/utils/file_io.h>

namespace z13 {
namespace {

constexpr std::string_view kTestDirectoryName = "z13_file_io_test";
constexpr std::string_view kNestedDirectoryName = "nested";
constexpr std::string_view kTestFileName = "file.txt";

class FileIoTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Per test: ctest runs them as parallel processes.
    directory_ = std::filesystem::temp_directory_path() / kTestDirectoryName /
                 ::testing::UnitTest::GetInstance()->current_test_info()->name();
    std::filesystem::remove_all(directory_);
  }

  void TearDown() override { std::filesystem::remove_all(directory_); }

  std::filesystem::path Path() const { return directory_ / kNestedDirectoryName / kTestFileName; }

  std::filesystem::path directory_;
};

TEST_F(FileIoTest, WriteCreatesDirectoriesAndReadReturnsTheSameBytes) {
  const std::string contents("line\r\nnext\0tail", 15);

  ASSERT_TRUE(WriteFile(Path(), contents).has_value());

  EXPECT_EQ(ReadFile(Path()), contents);
}

TEST_F(FileIoTest, WriteReplacesTheWholeFile) {
  ASSERT_TRUE(WriteFile(Path(), "a longer first version").has_value());
  ASSERT_TRUE(WriteFile(Path(), "short").has_value());

  EXPECT_EQ(ReadFile(Path()), "short");
}

TEST_F(FileIoTest, ReadingAMissingFileIsAnError) {
  const auto contents = ReadFile(Path());

  ASSERT_FALSE(contents.has_value());
  EXPECT_NE(contents.error().find(kTestFileName), std::string::npos) << contents.error();
}

TEST_F(FileIoTest, WritingOverADirectoryIsAnError) {
  std::filesystem::create_directories(Path());

  EXPECT_FALSE(WriteFile(Path(), "text").has_value());
}

}  // namespace
}  // namespace z13
