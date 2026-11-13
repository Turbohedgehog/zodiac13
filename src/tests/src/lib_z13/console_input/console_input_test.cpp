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

#include <format>
#include <optional>
#include <string>

#include <flecs.h>

#include <console_input/console_input.h>
#include <lib_core/commands/command_registry.h>
#include <lib_core/state/world_state.h>

namespace z13::console_input_test {

using console_input::ReplyText;
using console_input::SplitCommandLine;
using console_input::SubmitCommandLine;

struct AddCommand {
  using Command = z13::Command<std::string, "Adds two numbers.", Tags<"debug">>;

  int a {};
  int b {};
};

struct ScaleCommand {
  using Command = z13::Command<double, "Scales.", Tags<"debug">>;

  double factor {1.0};
  bool flip {};
  std::string label;
};

struct ShowCommand {
  using Command = z13::Command<std::string, "Shows what it is given.", Tags<"debug">>;

  std::optional<std::string> name;
  std::optional<int> count;
};

class ConsoleInputTest : public testing::Test {
 protected:
  void SetUp() override {
    flecs_tools::RegisterComponents<AddCommand, ScaleCommand, ShowCommand>(world_);
    z13::CommandSystem<AddCommand>(world_, "Add", [](const AddCommand& request, ReplyOf<AddCommand>& reply) {
      reply.Succeed(std::to_string(request.a + request.b));
    });
    z13::CommandSystem<ShowCommand>(world_, "Show", [](const ShowCommand& request, ReplyOf<ShowCommand>& reply) {
      reply.Succeed(std::format("{} {}", request.name.value_or("-"), request.count.value_or(0)));
    });
    z13::CommandSystem<ScaleCommand>(world_, "Scale", [](const ScaleCommand& request, ReplyOf<ScaleCommand>& reply) {
      reply.Succeed(request.flip ? -request.factor : request.factor);
    });
  }

  std::string Text(std::string_view line) {
    std::optional<CommandResult> reply;
    const Status submitted = SubmitCommandLine(world_, filter_, line, [&reply](CommandResult result) {
      reply = std::move(result);
    });
    if (!submitted) {
      return "error: " + submitted.error();
    }
    world_.progress();
    if (!reply) {
      return "no reply";
    }
    return *reply ? ReplyText(**reply) : "error: " + reply->error();
  }

  flecs::world world_;
  CommandFilter filter_ {.tags = {"debug"}};
};

TEST(CommandLineTest, SplitsWordsHonoringQuotesAndBackslashes) {
  const auto words = SplitCommandLine(R"(say "two words" a\ b "")");
  ASSERT_TRUE(words);
  EXPECT_EQ(*words, (std::vector<std::string> {"say", "two words", "a b", ""}));
  EXPECT_FALSE(SplitCommandLine("\"open"));
  EXPECT_FALSE(SplitCommandLine("end\\"));
}

TEST_F(ConsoleInputTest, WordsGoToFieldsPositionallyOrByName) {
  EXPECT_EQ(Text("add 2 3"), "5");
  EXPECT_EQ(Text("add b=10 1"), "11");
  EXPECT_EQ(Text("add 4"), "4");
}

TEST_F(ConsoleInputTest, EachWordIsValuedByTheKindOfItsField) {
  EXPECT_EQ(Text("scale 2 true \"a b\""), "-2.0");
  EXPECT_EQ(Text("scale 3"), "3.0");
  EXPECT_EQ(Text("scale 3 false"), "3.0");
}

TEST_F(ConsoleInputTest, AnOptionalArgumentIsSpelledAsItsValueAndMayBeLeftOut) {
  EXPECT_EQ(Text("show"), "- 0");
  EXPECT_EQ(Text("show echo"), "echo 0");
  EXPECT_EQ(Text("show echo 3"), "echo 3");
  EXPECT_NE(Text("show echo x").find("not an integer"), std::string::npos);
}

TEST_F(ConsoleInputTest, SaysWhatIsWrongWithALine) {
  EXPECT_NE(Text("add x 1").find("not an integer"), std::string::npos);
  EXPECT_NE(Text("scale 1 maybe").find("true or false"), std::string::npos);
  EXPECT_NE(Text("add 1 2 3").find("takes 2"), std::string::npos);
  EXPECT_NE(Text("nothing").find("unknown command"), std::string::npos);
  EXPECT_NE(Text("").find("empty line"), std::string::npos);
}

}  // namespace z13::console_input_test
