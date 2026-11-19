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

#include <lib_core/commands/command_registry.h>
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

class EditorOverlayTest : public ::testing::Test {
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

TEST_F(EditorOverlayTest, TheGraphLayerIsRegisteredAndEmptyWhileNotShown) {
  EXPECT_TRUE(GraphLayer(test_world_).boxes.empty());
}

TEST_F(EditorOverlayTest, ShownItHoldsABoxPerRoomAndPerEdgeAndSaysHowManyProblemsThereAre) {
  Show();

  const z13::station::OverlayLayer& layer = GraphLayer(test_world_);

  EXPECT_EQ(layer.boxes.size(), kPrototypeRooms + kPrototypeEdges);
  EXPECT_NE(layer.label.find("3 rooms, 2 edges, 0 problems"), std::string::npos) << layer.label;
}

TEST_F(EditorOverlayTest, AnEditShowsAtOnceAndAnEdgeWithoutAWallHasNoBox) {
  ASSERT_TRUE(RunCommand(test_world_, "editor_remove_edge", {{"id", rfl::Generic(std::string("hall-corridor"))}}));
  ASSERT_TRUE(RunCommand(test_world_, "editor_add_edge", {{"id", rfl::Generic(std::string("hall-lab"))},
                                                  {"from", rfl::Generic(std::string("hall"))},
                                                  {"to", rfl::Generic(std::string("lab"))}}));
  Show();

  const z13::station::OverlayLayer& layer = GraphLayer(test_world_);

  EXPECT_EQ(layer.boxes.size(), kPrototypeRooms + 1) << "the corridor-lab edge only";
  EXPECT_NE(layer.label.find("1 problems"), std::string::npos) << layer.label;
}

TEST_F(EditorOverlayTest, TheLayerEmptiesWhenItIsSteppedPast) {
  Show();
  test_world_.World().get_mut<z13::station::OverlayLayers>().shown.reset();
  test_world_.Tick();

  EXPECT_TRUE(GraphLayer(test_world_).boxes.empty());
}

}  // namespace
}  // namespace z13::station_editor
