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

#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <string_view>

#include <z13/constants/constants.h>
#include <z13_launcher/log_file.h>

namespace z13 {
namespace {

constexpr std::string_view kTestDirectoryName = "z13_log_file_test";
constexpr std::string_view kOtherFileName = "notes.log";
// More logs than are kept, so the oldest have to go.
constexpr std::size_t kWrittenLogs = kKeptLogFiles + 2;

class LogFileTest : public ::testing::Test {
 protected:
  void SetUp() override {
    directory_ = std::filesystem::temp_directory_path() / kTestDirectoryName /
                 ::testing::UnitTest::GetInstance()->current_test_info()->name();
    std::filesystem::remove_all(directory_);
    std::filesystem::create_directories(directory_);
  }

  void TearDown() override { std::filesystem::remove_all(directory_); }

  // Index order is age order, as with real start times.
  std::filesystem::path RunLog(std::size_t index) const {
    return directory_ / std::format("{}-20261016-0000{:02}.000.log", constants::kGameName, index);
  }

  void Touch(const std::filesystem::path& path) const { std::ofstream(path) << "line\n"; }

  std::filesystem::path directory_;
};

TEST_F(LogFileTest, RemovingOldLogsLeavesRoomForThisRun) {
  for (std::size_t index = 0; index < kWrittenLogs; ++index) {
    Touch(RunLog(index));
  }
  Touch(directory_ / kOtherFileName);

  ASSERT_TRUE(RemoveOldLogs(directory_).has_value());

  const std::size_t removed = kWrittenLogs - kKeptLogFiles + 1;
  for (std::size_t index = 0; index < kWrittenLogs; ++index) {
    EXPECT_EQ(std::filesystem::exists(RunLog(index)), index >= removed) << index;
  }
  EXPECT_TRUE(std::filesystem::exists(directory_ / kOtherFileName));
}

TEST_F(LogFileTest, AMissingDirectoryHasNothingToRemove) {
  EXPECT_TRUE(RemoveOldLogs(directory_ / "missing").has_value());
}

}  // namespace
}  // namespace z13
