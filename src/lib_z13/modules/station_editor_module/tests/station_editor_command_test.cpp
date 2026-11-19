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

#include <array>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>

#include <flecs.h>
#include <rfl/json.hpp>

#include <lib_core/commands/command_registry.h>
#include <lib_core/world/core.h>
#include <station_editor_module/station_editor_module_factory.h>
#include <station_graph/graph_json.h>
#include <z13_tests/shipped_station.h>

namespace z13::station_editor {
namespace {

constexpr std::string_view kEditorTagName = "editor";
constexpr std::string_view kPrototype = z13::testing::kPrototypeGraph;
constexpr std::int64_t kRoomCells = 12;
constexpr int kWiderLab = 30;  // the prototype's lab is 28 wide

// The module in a headless world over the source tree's assets, and the editor's commands asked as a channel would.
class StationEditorCommandTest : public ::testing::Test {
 protected:
  void SetUp() override {
    std::string program = "z13_test_runner";
    std::array<char*, 1> arguments = {program.data()};
    core_ = std::make_unique<z13::Core>(1, arguments.data());
    core_->SetAssetSource(z13::testing::SourceAssets());
    core_->RegisterModuleFactory(std::make_shared<StationEditorModuleFactory>());
    const auto created = core_->CreateWorld();
    ASSERT_TRUE(created) << created.error();
    world_ = created->get();
  }

  CommandResult Run(std::string_view name, std::initializer_list<std::pair<std::string, rfl::Generic>> fields = {}) {
    rfl::Generic::Object arguments;
    for (const auto& [field, value] : fields) {
      arguments[field] = value;
    }
    std::optional<CommandResult> reply;
    const Status submitted = SubmitCommand(world_, {.tags = {std::string(kEditorTagName)}}, name,
                                           rfl::Generic(std::move(arguments)),
                                           [&reply](CommandResult result) { reply = std::move(result); });
    if (!submitted) {
      return std::unexpected(submitted.error());
    }
    world_.progress();
    return reply.value_or(std::unexpected("no reply"));
  }

  std::string Text(const CommandResult& reply) {
    if (!reply) {
      return "error: " + reply.error();
    }
    return rfl::json::write(*reply);
  }

  // A reply that answers a string, unquoted: the graph's JSON, which Text would quote.
  static std::string ShownText(const CommandResult& reply) { return reply->to_string().value_or(""); }

  void LoadPrototype() { ASSERT_TRUE(Run("editor_load", {{"name", rfl::Generic(std::string(kPrototype))}})); }

  static rfl::Generic Int(std::int64_t value) { return rfl::Generic(value); }
  static rfl::Generic Str(std::string value) { return rfl::Generic(std::move(value)); }

  std::unique_ptr<z13::Core> core_;
  flecs::world world_;
};

TEST_F(StationEditorCommandTest, EveryCommandHasAHandler) {
  EXPECT_TRUE(CommandsWithoutHandler(world_).empty());
}

TEST_F(StationEditorCommandTest, NothingIsEditedBeforeAGraphIsLoaded) {
  const CommandResult reply = Run("editor_remove_room", {{"id", Str("hall")}});

  ASSERT_FALSE(reply);
  EXPECT_NE(reply.error().find("editor_load"), std::string::npos);
}

TEST_F(StationEditorCommandTest, LoadsTheGraphAndShowsItAsItsFile) {
  const CommandResult loaded = Run("editor_load", {{"name", Str(std::string(kPrototype))}});
  ASSERT_TRUE(loaded) << loaded.error();
  EXPECT_NE(Text(loaded).find("3 rooms, 2 edges"), std::string::npos);

  const CommandResult shown = Run("editor_show");
  ASSERT_TRUE(shown);
  const auto parsed = z13::editor::graph::ParseGraph(ShownText(shown));
  ASSERT_TRUE(parsed) << parsed.error();
  EXPECT_EQ(*parsed, z13::testing::ShippedGraph(kPrototype).value());
  EXPECT_FALSE(Run("editor_load", {{"name", Str("nowhere")}}));
}

TEST_F(StationEditorCommandTest, EditsTheGraphAndUndoTakesThemBack) {
  LoadPrototype();

  ASSERT_TRUE(Run("editor_set_room", {{"id", Str("annex")}, {"y", Int(24 + kRoomCells)}, {"w", Int(kRoomCells)}, {"d", Int(kRoomCells)}}));
  ASSERT_TRUE(Run("editor_set_property", {{"target", Str("node")}, {"id", Str("annex")}, {"name", Str("height")}, {"value", Str("20")}}));
  EXPECT_NE(Text(Run("editor_show")).find("annex"), std::string::npos);

  ASSERT_TRUE(Run("editor_undo"));
  ASSERT_TRUE(Run("editor_undo"));
  EXPECT_EQ(Text(Run("editor_show")).find("annex"), std::string::npos);
  ASSERT_TRUE(Run("editor_redo"));
  EXPECT_NE(Text(Run("editor_show")).find("annex"), std::string::npos);
}

TEST_F(StationEditorCommandTest, SettingARoomThatExistsMovesIt) {
  LoadPrototype();
  const std::string before = Text(Run("editor_show"));

  ASSERT_TRUE(Run("editor_set_room", {{"id", Str("lab")}, {"x", Int(79)}, {"y", Int(0)}, {"z", Int(0)}, {"w", Int(kWiderLab)}, {"d", Int(24)}}));

  EXPECT_NE(Text(Run("editor_show")), before);
  const auto moved = z13::editor::graph::ParseGraph(ShownText(Run("editor_show")));
  ASSERT_TRUE(moved) << moved.error();
  const auto lab = z13::editor::graph::FindNode(*moved, "lab");
  ASSERT_TRUE(lab);
  EXPECT_EQ(moved->nodes[*lab].box.size.x(), kWiderLab);
}

TEST_F(StationEditorCommandTest, ARefusedEditSaysWhyAndChangesNothing) {
  LoadPrototype();
  const std::string before = Text(Run("editor_show"));

  const CommandResult range = Run("editor_set_property", {{"target", Str("node")}, {"id", Str("hall")}, {"name", Str("height")}, {"value", Str("99")}});
  const CommandResult unknown = Run("editor_set_property", {{"target", Str("node")}, {"id", Str("hall")}, {"name", Str("colour")}, {"value", Str("red")}});
  const CommandResult tiny = Run("editor_set_room", {{"id", Str("tiny")}, {"w", Int(1)}, {"d", Int(1)}});
  const CommandResult target = Run("editor_set_property", {{"target", Str("wall")}, {"id", Str("hall")}, {"name", Str("height")}, {"value", Str("20")}});

  ASSERT_FALSE(range);
  EXPECT_NE(range.error().find("12..40"), std::string::npos) << range.error();
  ASSERT_FALSE(unknown);
  EXPECT_NE(unknown.error().find("colour"), std::string::npos);
  EXPECT_FALSE(tiny);
  EXPECT_FALSE(target);
  EXPECT_EQ(Text(Run("editor_show")), before);
}

TEST_F(StationEditorCommandTest, DiagnoseListsWhatCannotBeBuiltAndSaveWritesTheFile) {
  LoadPrototype();
  EXPECT_EQ(Text(Run("editor_diagnose")), "[]");
  ASSERT_TRUE(Run("editor_remove_edge", {{"id", Str("hall-corridor")}}));
  ASSERT_TRUE(Run("editor_add_edge", {{"id", Str("hall-lab")}, {"from", Str("hall")}, {"to", Str("lab")}}));
  EXPECT_NE(Text(Run("editor_diagnose")).find("hall-lab"), std::string::npos);

  const std::filesystem::path path = std::filesystem::temp_directory_path() / "z13_station_editor_test.json";
  ASSERT_TRUE(Run("editor_save", {{"path", Str(path.string())}}));
  std::ifstream file(path);
  const std::string saved((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  std::filesystem::remove(path);
  EXPECT_NE(saved.find("hall-lab"), std::string::npos);
}

}  // namespace
}  // namespace z13::station_editor
