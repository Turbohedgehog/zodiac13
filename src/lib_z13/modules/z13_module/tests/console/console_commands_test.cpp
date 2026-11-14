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
#include <vector>

#include <spdlog/spdlog.h>

#include <console_input/console_completion.h>
#include <console_input/console_session.h>
#include <lib_core/utils/console_log.h>

#include "../support/z13_test_world.h"
#include "field_sim.h"

namespace z13 {
namespace {

constexpr int kFramesForAReply = 3;
// The reply of setting the value and the reply of reading it back.
constexpr int kShownAfterSettingAndReading = 2;

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

TEST_F(ConsoleCommandsTest, TheGhostIsTheRestOfTheOnlyMatch) {
  const flecs::world world = test_world_.World();
  const auto ghost = [&world](std::string_view line) {
    return console_input::GhostOf(line, console_input::CompletionOf(world, line));
  };

  EXPECT_EQ(ghost("q"), "uit");
  EXPECT_EQ(ghost("ech"), "o");
  EXPECT_EQ(ghost("e"), "");
  EXPECT_EQ(ghost("quit"), "");
  EXPECT_EQ(ghost("quit "), "");
  EXPECT_EQ(ghost("zzz"), "");
}

// The cases of the field's completion as the player met them on the tablet.
TEST_F(ConsoleCommandsTest, FieldTheOnlyMatchIsShownSelectedAndTypingOverItKeepsItUntilTheWordIsDone) {
  z13::testing::FieldSim field(test_world_.World());

  field.Type("q");
  field.Frame();
  EXPECT_EQ(field.Text(), "quit");
  EXPECT_EQ(field.Selected(), "uit");

  field.Type("u");
  field.Frame();
  EXPECT_EQ(field.Text(), "quit");
  EXPECT_EQ(field.Selected(), "it");

  field.Type("i");
  field.Frame();
  field.Type("t");
  field.Frame();
  EXPECT_EQ(field.Text(), "quit");
  EXPECT_EQ(field.Selected(), "");
  EXPECT_FALSE(field.ListOpen());
}

TEST_F(ConsoleCommandsTest, FieldSeveralMatchesOpenTheListAndTypingOnNarrowsItToTheOnlyMatch) {
  z13::testing::FieldSim field(test_world_.World());

  field.Type("e");
  field.Frame();
  EXPECT_TRUE(field.ListOpen());
  EXPECT_EQ(field.Selected(), "");

  field.Type("c");
  field.Frame();
  EXPECT_FALSE(field.ListOpen());
  EXPECT_EQ(field.Text(), "echo");
  EXPECT_EQ(field.Selected(), "ho");
}

TEST_F(ConsoleCommandsTest, FieldTwoCharactersInOneFrameAreCompletedLikeTwoInTwo) {
  z13::testing::FieldSim field(test_world_.World());

  field.Type("ec");
  field.Frame();

  EXPECT_EQ(field.Text(), "echo");
  EXPECT_EQ(field.Selected(), "ho");
}

TEST_F(ConsoleCommandsTest, FieldBackspaceClearsTheRestFirstAndBringsTheListBackAtTheShortWord) {
  z13::testing::FieldSim field(test_world_.World());
  field.Type("e");
  field.Frame();
  field.Type("c");
  field.Frame();

  field.Backspace();
  field.Frame();
  EXPECT_EQ(field.Text(), "ec");
  EXPECT_EQ(field.Selected(), "");
  EXPECT_FALSE(field.ListOpen());

  field.Backspace();
  field.Frame();
  EXPECT_EQ(field.Text(), "e");
  EXPECT_EQ(field.Selected(), "");
  EXPECT_TRUE(field.ListOpen());

  field.Type("x");
  field.Frame();
  EXPECT_EQ(field.Text(), "exit");
  EXPECT_FALSE(field.ListOpen());
}

TEST_F(ConsoleCommandsTest, FieldBackspaceUntilEmptyShowsNothing) {
  z13::testing::FieldSim field(test_world_.World());
  field.Type("q");
  field.Frame();

  field.Backspace();
  field.Frame();
  EXPECT_EQ(field.Text(), "q");
  field.Backspace();
  field.Frame();

  EXPECT_EQ(field.Text(), "");
  EXPECT_FALSE(field.ListOpen());
}

TEST_F(ConsoleCommandsTest, FieldAnEmptyLastWordOfferedNothing) {
  z13::testing::FieldSim field(test_world_.World());

  field.Type("set ");
  field.Frame();

  EXPECT_EQ(field.Selected(), "");
  EXPECT_FALSE(field.ListOpen());
}

TEST_F(ConsoleCommandsTest, FieldTextTheProgramPutInIsNeitherTypedNorDeleted) {
  z13::testing::FieldSim field(test_world_.World());

  field.Replace("qu");
  field.Frame();

  EXPECT_EQ(field.Text(), "qu");
  EXPECT_EQ(field.Selected(), "");
}

TEST_F(ConsoleCommandsTest, FieldWithTheCursorAwayFromTheEndNothingIsOffered) {
  z13::testing::FieldSim field(test_world_.World());
  field.MoveCursorAwayFromTheEnd();

  field.Type("q");
  field.Frame();

  EXPECT_EQ(field.Text(), "q");
  EXPECT_FALSE(field.ListOpen());
}

TEST_F(ConsoleCommandsTest, FieldSeveralPathsOfSettingsOpenTheListAfterSet) {
  z13::testing::FieldSim field(test_world_.World());

  field.Type("set render.s");
  field.Frame();

  EXPECT_TRUE(field.ListOpen());
  EXPECT_EQ(field.Selected(), "");
}

TEST_F(ConsoleCommandsTest, QuitAndExitAreCommandsOfTheConsole) {
  Run("help");
  EXPECT_TRUE(LoggedPart("quit"));
  EXPECT_TRUE(LoggedPart("exit"));

  Run("quit");
  Run("exit");
  const auto lines = log().Lines();
  EXPECT_FALSE(std::ranges::any_of(lines, [](const LogLine& line) { return line.level == spdlog::level::err; }));
}

TEST_F(ConsoleCommandsTest, AnEmptyLineDoesNothing) {
  const std::uint64_t before = log().Revision();

  Run("   ");

  EXPECT_EQ(log().Revision(), before);
}

TEST_F(ConsoleCommandsTest, ASettingsPathAloneShowsItsValueAndWithAValueChangesIt) {
  Run("render.shadow_lights 7");
  EXPECT_TRUE(Logged("render.shadow_lights = 7"));

  Run("render.shadow_lights");
  EXPECT_TRUE(Logged("> render.shadow_lights"));
  const auto lines = log().Lines();
  const auto shown = std::ranges::count_if(lines, [](const LogLine& line) {
    return line.text == "render.shadow_lights = 7";
  });
  EXPECT_EQ(shown, kShownAfterSettingAndReading);
}

TEST_F(ConsoleCommandsTest, ABadValueOfASettingIsAnError) {
  Run("render.shadow_lights many");

  const auto lines = log().Lines();
  EXPECT_TRUE(std::ranges::any_of(lines, [](const LogLine& line) { return line.level == spdlog::level::err; }));
}

TEST_F(ConsoleCommandsTest, ATableNameWithADotListsItsSettings) {
  Run("render.");

  EXPECT_TRUE(LoggedPart("render.shadow_lights = "));
}

TEST_F(ConsoleCommandsTest, TabCompletesCommandsAndPaths) {
  const flecs::world world = test_world_.World();

  const auto commands = console_input::CompletionOf(world, "ech");
  EXPECT_EQ(commands.candidates, std::vector<std::string> {"echo"});
  EXPECT_EQ(console_input::Completed("ech", commands, commands.candidates.front()), "echo ");

  const auto paths = console_input::CompletionOf(world, "render.shadow_l");
  ASSERT_FALSE(paths.candidates.empty());
  EXPECT_TRUE(std::ranges::contains(paths.candidates, "render.shadow_lights"));
  EXPECT_EQ(console_input::CommonPrefix(paths), "render.shadow_lights");

  const auto after_set = console_input::CompletionOf(world, "set render.shadow_l");
  EXPECT_EQ(after_set.word_begin, 4U);
  EXPECT_TRUE(std::ranges::contains(after_set.candidates, "render.shadow_lights"));

  EXPECT_TRUE(console_input::CompletionOf(world, "echo e").candidates.empty());
}

}  // namespace
}  // namespace z13
