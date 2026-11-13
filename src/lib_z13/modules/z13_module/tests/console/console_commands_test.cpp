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

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>

#include <spdlog/spdlog.h>

#include <console_input/console_session.h>
#include <lib_core/utils/console_log.h>

#include "../support/z13_test_world.h"

namespace z13 {
namespace {

constexpr int kFramesForAReply = 3;

// The world's console log, fed from spdlog for the test's length.
class ConsoleCommandsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    sink_ = log().Sink();
    spdlog::default_logger()->sinks().push_back(sink_);
  }

  void TearDown() override { std::erase(spdlog::default_logger()->sinks(), sink_); }

  const ConsoleLog& log() { return test_world_.World().get<ConsoleLog>(); }

  void Run(std::string_view line) {
    console_input::RunConsoleLine(test_world_.World(), line);
    for (int frame = 0; frame < kFramesForAReply; ++frame) {
      test_world_.Tick();
    }
  }

  bool Logged(std::string_view text) {
    const auto lines = log().Lines();
    return std::ranges::any_of(lines, [text](const LogLine& line) { return line.text == text; });
  }

  bool LoggedPart(std::string_view text) {
    const auto lines = log().Lines();
    return std::ranges::any_of(lines, [text](const LogLine& line) { return line.text.contains(text); });
  }

  z13::testing::Z13TestWorld test_world_;
  std::shared_ptr<spdlog::sinks::sink> sink_;
};

TEST_F(ConsoleCommandsTest, TheLineIsLoggedAndEchoAnswersWithItsText) {
  Run("echo \"hello world\"");

  EXPECT_TRUE(Logged("> echo \"hello world\""));
  EXPECT_TRUE(Logged("hello world"));
}

TEST_F(ConsoleCommandsTest, HelpListsTheCommandsAndDescribesOne) {
  Run("help");
  EXPECT_TRUE(LoggedPart("echo <text>"));
  EXPECT_TRUE(LoggedPart("help <name>"));

  Run("help echo");
  EXPECT_TRUE(LoggedPart("Answers with `text`."));
}

TEST_F(ConsoleCommandsTest, ClearEmptiesTheLog) {
  Run("echo something");
  ASSERT_TRUE(Logged("something"));

  Run("clear");

  EXPECT_FALSE(Logged("something"));
}

TEST_F(ConsoleCommandsTest, AnUnknownCommandIsAnError) {
  Run("nonsense 1");

  const auto lines = log().Lines();
  const auto error = std::ranges::find_if(lines, [](const LogLine& line) { return line.level == spdlog::level::err; });
  ASSERT_NE(error, lines.end());
  EXPECT_TRUE(error->text.contains("nonsense"));
}

TEST_F(ConsoleCommandsTest, AnEmptyLineDoesNothing) {
  const std::uint64_t before = log().Revision();

  Run("   ");

  EXPECT_EQ(log().Revision(), before);
}

}  // namespace
}  // namespace z13
