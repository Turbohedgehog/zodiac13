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
#include <optional>
#include <string>

#include <lib_core/commands/command_registry.h>
#include <z13/components/editor/editor_view.h>
#include <z13/components/station/overlay_layers.h>

#include "../../z13_module/tests/support/z13_test_world.h"

namespace z13::station_editor {
namespace {

using z13::testing::Z13TestWorld;

constexpr std::string_view kEditorTagName = "editor";
constexpr std::string_view kLayerName = "graph";
constexpr std::string_view kPrototype = "prototype";
constexpr std::size_t kPrototypeRooms = 3;
constexpr std::size_t kPrototypeEdges = 2;

CommandResult RunCommand(Z13TestWorld& test_world, std::string_view name,
                  std::initializer_list<std::pair<std::string, rfl::Generic>> fields = {}) {
  rfl::Generic::Object arguments;
  for (const auto& [field, value] : fields) {
    arguments[field] = value;
  }
  std::optional<CommandResult> reply;
  const Status submitted = SubmitCommand(test_world.World(), {.tags = {std::string(kEditorTagName)}}, name,
                                         rfl::Generic(std::move(arguments)),
                                         [&reply](CommandResult result) { reply = std::move(result); });
  if (!submitted) {
    return std::unexpected(submitted.error());
  }
  test_world.Tick();
  return reply.value_or(std::unexpected("no reply"));
}

const z13::station::OverlayLayer& GraphLayer(Z13TestWorld& test_world) {
  return *test_world.World().get<z13::station::OverlayLayers>().Find(std::string(kLayerName));
}

class EditorDisplayTest : public ::testing::Test {
 protected:
  void SetUp() override {
    test_world_.Tick();
    ASSERT_TRUE(RunCommand(test_world_, "editor_load", {{"name", rfl::Generic(std::string(kPrototype))}}));
  }

  void Show() {
    test_world_.World().get_mut<z13::station::OverlayLayers>().shown = std::string(kLayerName);
    test_world_.Tick();
  }

  Z13TestWorld test_world_;
};

TEST_F(EditorDisplayTest, TheGraphLayerIsRegisteredAndEmptyWhileNotShown) {
  EXPECT_TRUE(GraphLayer(test_world_).boxes.empty());
}

TEST_F(EditorDisplayTest, ShownItHoldsABoxPerRoomAndPerEdgeAndSaysHowManyProblemsThereAre) {
  Show();

  const z13::station::OverlayLayer& layer = GraphLayer(test_world_);

  EXPECT_EQ(layer.boxes.size(), kPrototypeRooms + kPrototypeEdges);
  EXPECT_NE(layer.label.find("3 rooms, 2 edges, 0 problems"), std::string::npos) << layer.label;
}

TEST_F(EditorDisplayTest, AnEditShowsAtOnceAndAnEdgeWithoutAWallHasNoBox) {
  ASSERT_TRUE(RunCommand(test_world_, "editor_remove_edge", {{"id", rfl::Generic(std::string("hall-corridor"))}}));
  ASSERT_TRUE(RunCommand(test_world_, "editor_add_edge", {{"id", rfl::Generic(std::string("hall-lab"))},
                                                  {"from", rfl::Generic(std::string("hall"))},
                                                  {"to", rfl::Generic(std::string("lab"))}}));
  Show();

  const z13::station::OverlayLayer& layer = GraphLayer(test_world_);

  EXPECT_EQ(layer.boxes.size(), kPrototypeRooms + 1) << "the corridor-lab edge only";
  EXPECT_NE(layer.label.find("1 problems"), std::string::npos) << layer.label;
}

TEST_F(EditorDisplayTest, TheLayerEmptiesWhenItIsSteppedPast) {
  Show();
  test_world_.World().get_mut<z13::station::OverlayLayers>().shown.reset();
  test_world_.Tick();

  EXPECT_TRUE(GraphLayer(test_world_).boxes.empty());
}

const z13::editor::EditorItemView& RoomOf(Z13TestWorld& test_world, std::string_view id) {
  const auto& rooms = test_world.World().get<z13::editor::EditorView>().rooms;
  return *std::ranges::find(rooms, id, &z13::editor::EditorItemView::id);
}

const z13::editor::EditorPropertyView& PropertyOf(const z13::editor::EditorItemView& item, std::string_view name) {
  return *std::ranges::find(item.properties, name, &z13::editor::EditorPropertyView::name);
}

TEST_F(EditorDisplayTest, TheViewStaysEmptyWhileTheWindowIsClosed) {
  EXPECT_FALSE(test_world_.World().get<z13::editor::EditorView>().loaded);
  EXPECT_TRUE(test_world_.World().get<z13::editor::EditorView>().rooms.empty());
}

TEST_F(EditorDisplayTest, TheWindowCommandOpensItAndTheViewHoldsTheGraphWithTheSchemasProperties) {
  ASSERT_TRUE(RunCommand(test_world_, "editor_window"));
  test_world_.Tick();

  const z13::editor::EditorView& view = test_world_.World().get<z13::editor::EditorView>();

  EXPECT_TRUE(view.open);
  EXPECT_TRUE(view.loaded);
  EXPECT_EQ(view.rooms.size(), kPrototypeRooms);
  EXPECT_EQ(view.edges.size(), kPrototypeEdges);
  EXPECT_TRUE(view.problems.empty());
  const auto& height = PropertyOf(RoomOf(test_world_, "hall"), "height");
  EXPECT_EQ(height.kind, z13::editor::EditorPropertyKind::kInt);
  EXPECT_LT(height.min, height.max);
  EXPECT_FALSE(height.value.empty());
}

TEST_F(EditorDisplayTest, AnEditChangesTheValueTheViewShowsAndTheWindowCommandClosesIt) {
  ASSERT_TRUE(RunCommand(test_world_, "editor_window"));
  test_world_.Tick();
  const int taller = std::stoi(PropertyOf(RoomOf(test_world_, "hall"), "height").value) + 1;

  ASSERT_TRUE(RunCommand(test_world_, "editor_set_property",
                         {{"target", rfl::Generic(std::string("node"))},
                          {"id", rfl::Generic(std::string("hall"))},
                          {"name", rfl::Generic(std::string("height"))},
                          {"value", rfl::Generic(std::to_string(taller))}}));
  test_world_.Tick();

  EXPECT_EQ(PropertyOf(RoomOf(test_world_, "hall"), "height").value, std::to_string(taller));
  ASSERT_TRUE(RunCommand(test_world_, "editor_window"));
  EXPECT_FALSE(test_world_.World().get<z13::editor::EditorView>().open);
}

}  // namespace
}  // namespace z13::station_editor
