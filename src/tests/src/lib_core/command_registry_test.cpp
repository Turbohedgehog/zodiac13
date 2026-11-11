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

#include <optional>
#include <string>

#include <flecs.h>

#include <lib_core/commands/command_registry.h>
#include <lib_core/state/world_state.h>

namespace z13::commands_test {

struct AddCommand {
  using Command = z13::Command<std::string, "Adds two numbers.", Tags<"debug">>;

  int a {};
  int b {};
};

struct Shout {
  using Command = z13::Command<std::string, "Repeats a word.", Tags<"debug", "cheat">, CommandName<"say_loudly">>;

  std::string word;
};

struct Point {
  int x {};
  int y {};
};

struct WherePoint {
  using Command = z13::Command<Point, "Returns a point.", Tags<"cheat">>;

  int x {};
};

struct Orphan {
  using Command = z13::Command<std::string, "Nothing handles it.", Tags<"debug">>;

  int level {};
};

class CommandRegistryTest : public testing::Test {
 protected:
  void SetUp() override {
    flecs_tools::RegisterComponents<AddCommand, Shout, WherePoint, Orphan>(world_);
    z13::CommandSystem<AddCommand>(world_, "Add", [](const AddCommand& request, ReplyOf<AddCommand>& reply) {
      reply.Succeed(std::to_string(request.a + request.b));
    });
    z13::CommandSystem<Shout>(world_, "Shout", [](const Shout& request, ReplyOf<Shout>& reply) {
      if (request.word.empty()) {
        reply.Fail("nothing to say");
      } else {
        reply.Succeed(request.word + "!");
      }
    });
    z13::CommandSystem<WherePoint>(world_, "WherePoint", [](const WherePoint& request, ReplyOf<WherePoint>& reply) {
      reply.Succeed(Point {.x = request.x, .y = 2});
    });
  }

  CommandResultText Run(const CommandFilter& filter, std::string_view line) {
    std::optional<CommandResultText> reply;
    const Status submitted = SubmitCommandLine(world_, filter, line, [&reply](CommandResultText text) {
      reply = std::move(text);
    });
    if (!submitted) {
      return std::unexpected(submitted.error());
    }
    world_.progress();
    return reply.value_or(std::unexpected("no reply"));
  }

  std::string Text(const CommandFilter& filter, std::string_view line) {
    const CommandResultText reply = Run(filter, line);
    return reply ? *reply : "error: " + reply.error();
  }

  flecs::world world_;
  CommandFilter debug_ {.tags = {"debug"}};
  CommandFilter cheat_ {.tags = {"cheat"}};
};

TEST(CommandNameTest, TakesTheTypeNameWithoutTheSuffixInSnakeCase) {
  EXPECT_EQ(CommandNameOf("TeleportCommand"), "teleport");
  EXPECT_EQ(CommandNameOf("SetShadows"), "set_shadows");
  EXPECT_EQ(CommandNameOf("Command"), "command");
}

TEST(CommandLineTest, SplitsWordsHonoringQuotesAndBackslashes) {
  const auto words = SplitCommandLine(R"(say "two words" a\ b "")");
  ASSERT_TRUE(words);
  EXPECT_EQ(*words, (std::vector<std::string> {"say", "two words", "a b", ""}));
  EXPECT_FALSE(SplitCommandLine("\"open"));
  EXPECT_FALSE(SplitCommandLine("end\\"));
}

TEST_F(CommandRegistryTest, AHandlerAnswersALineWithPositionalAndNamedArguments) {
  EXPECT_EQ(Text(debug_, "add 2 3"), "5");
  EXPECT_EQ(Text(debug_, "add b=10 1"), "11");
  EXPECT_EQ(Text(debug_, "add 4"), "4");
}

TEST_F(CommandRegistryTest, ExplicitNameReplacesTheTypesAndAHandlerCanFail) {
  EXPECT_EQ(Text(debug_, "say_loudly 123"), "123!");
  EXPECT_EQ(Run(debug_, "say_loudly \"\"").error(), "nothing to say");
}

TEST_F(CommandRegistryTest, RejectsBadArgumentsAndUnknownCommandsWithAMessage) {
  EXPECT_NE(Run(debug_, "add x 1").error().find("not a number"), std::string::npos);
  EXPECT_NE(Run(debug_, "add 1 2 3").error().find("takes 2"), std::string::npos);
  EXPECT_NE(Run(debug_, "nothing").error().find("unknown command"), std::string::npos);
}

TEST_F(CommandRegistryTest, ATagLimitsWhatAChannelSees) {
  EXPECT_NE(Run(debug_, "where_point 1").error().find("unknown command"), std::string::npos);
  EXPECT_TRUE(Run(cheat_, "where_point 1"));
  EXPECT_TRUE(Run(cheat_, "say_loudly a"));
  EXPECT_FALSE(Run(cheat_, "add 1 2"));
  EXPECT_FALSE(Run(CommandFilter {}, "add 1 2"));
}

TEST_F(CommandRegistryTest, AStructResultBecomesJson) {
  EXPECT_EQ(Text(cheat_, "where_point 7"), R"({"x":7,"y":2})");
}

TEST_F(CommandRegistryTest, MoreThanOneHandlerSeesAndChangesTheReply) {
  z13::CommandSystem<AddCommand>(world_, "Double", [](const AddCommand&, ReplyOf<AddCommand>& reply) {
    reply.Succeed(reply.value + reply.value);
  });
  EXPECT_EQ(Text(debug_, "add 2 3"), "55");
}

TEST_F(CommandRegistryTest, ACommandWithoutAHandlerIsRefusedAndListed) {
  EXPECT_NE(Run(debug_, "orphan").error().find("no handler"), std::string::npos);
  EXPECT_EQ(CommandsWithoutHandler(world_), std::vector<std::string> {"orphan"});
}

TEST_F(CommandRegistryTest, KeepsTheSchemaAndDescriptionForTheBridge) {
  const auto& entry = world_.get<Commands>().entries.at("add");
  EXPECT_EQ(entry.description, "Adds two numbers.");
  EXPECT_NE(entry.schema.find("\"a\""), std::string::npos);
  ASSERT_EQ(entry.arguments.size(), 2U);
  EXPECT_EQ(entry.arguments.front().name, "a");
}

}  // namespace z13::commands_test
