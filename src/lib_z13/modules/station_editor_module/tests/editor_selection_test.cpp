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

#include <string>

#include <Eigen/Dense>

#include <player/gameplay_entities.h>
#include <z13/components/editor/editor_view.h>
#include <z13/components/gameplay/player.h>
#include <z13/components/station/block_spec.h>
#include <z13_tests/shipped_station.h>

#include "editor_test_support.h"

namespace z13::station_editor {
namespace {

using z13::editor::EditorTarget;
using z13::testing::Z13TestWorld;

constexpr std::string_view kScene = "prototype";
constexpr float kOutsideCells = -10.f;  // west of the hall, which starts at x = 0
constexpr float kHallMiddleCells = 12.f;
constexpr float kEyeHeightCells = 4.f;

rfl::Generic Str(std::string value) {
  return rfl::Generic(std::move(value));
}

// The local player moved to `cells`, looking along +x (an identity pose).
void PlacePlayer(Z13TestWorld& test_world, const Eigen::Vector3f& cells) {
  const auto id = test_world.World().get<z13::gameplay::LocalPlayer>().id;
  Eigen::Matrix4f pose = Eigen::Matrix4f::Identity();
  pose.block<3, 1>(0, 3) = cells * z13::station::kCellSize;
  test_world.World().lookup(z13::gameplay::PlayerEntityName(id.value()).c_str()).set<Eigen::Matrix4f>(pose);
}

const std::optional<z13::editor::EditorSelection>& Selected(Z13TestWorld& test_world) {
  return test_world.World().get<z13::editor::EditorView>().selected;
}

class EditorSelectionTest : public ::testing::Test {
 protected:
  void SetUp() override {
    test_world_.Tick();
    ASSERT_TRUE(RunCommand(test_world_, "editor_load", {{"name", Str(std::string(z13::testing::kPrototypeGraph))}}));
  }

  Z13TestWorld test_world_ {{std::string(z13::testing::kStationSceneArg), std::string(kScene)}};
};

TEST_F(EditorSelectionTest, SelectingARoomOrAnEdgeByNameKeepsItAndAnUnknownOneIsRefused) {
  ASSERT_TRUE(RunCommand(test_world_, "editor_select", {{"target", Str("node")}, {"id", Str("lab")}}));
  EXPECT_EQ(Selected(test_world_), (z13::editor::EditorSelection {.target = EditorTarget::kRoom, .id = "lab"}));

  ASSERT_TRUE(RunCommand(test_world_, "editor_select", {{"target", Str("edge")}, {"id", Str("hall-corridor")}}));
  EXPECT_EQ(Selected(test_world_)->target, EditorTarget::kEdge);

  EXPECT_FALSE(RunCommand(test_world_, "editor_select", {{"target", Str("node")}, {"id", Str("nowhere")}}));
  EXPECT_FALSE(RunCommand(test_world_, "editor_select", {{"target", Str("wall")}, {"id", Str("lab")}}));
  EXPECT_EQ(Selected(test_world_)->target, EditorTarget::kEdge) << "a refused pick changes nothing";
}

TEST_F(EditorSelectionTest, PickingTakesTheRoomThePlayerLooksAt) {
  PlacePlayer(test_world_, {kOutsideCells, kHallMiddleCells, kEyeHeightCells});

  const CommandResult reply = RunCommand(test_world_, "editor_pick");

  ASSERT_TRUE(reply) << reply.error();
  EXPECT_EQ(Selected(test_world_), (z13::editor::EditorSelection {.target = EditorTarget::kRoom, .id = "hall"}));
}

TEST_F(EditorSelectionTest, PickingWhereNothingIsAimedAtIsRefused) {
  PlacePlayer(test_world_, {kOutsideCells, -kHallMiddleCells * 10.f, kEyeHeightCells});

  EXPECT_FALSE(RunCommand(test_world_, "editor_pick"));
  EXPECT_FALSE(Selected(test_world_));
}

}  // namespace
}  // namespace z13::station_editor
