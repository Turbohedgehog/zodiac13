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

#include <Eigen/Dense>

#include <lib_core/commands/command_registry.h>
#include <lib_core/state/world_state.h>
#include <lib_core/utils/math.h>
#include <player/gameplay_entities.h>
#include <z13/components/gameplay/gameplay.h>
#include <z13/components/gameplay/player.h>
#include <z13/components/station/block.h>

#include "../../z13_module/tests/support/z13_test_world.h"

namespace z13::station_editor {
namespace {

using z13::testing::Z13TestWorld;

constexpr std::string_view kEditorTagName = "editor";
constexpr std::string_view kScene = "prototype";
constexpr std::string_view kMarkerName = "ReloadMarker";
constexpr float kMovedBy = 3.F;
constexpr float kPoseTolerance = 1e-4F;  // a position is copied, not computed: only float noise

CommandResult Reload(Z13TestWorld& test_world, std::string_view scene = {}) {
  rfl::Generic::Object arguments;
  arguments["scene"] = rfl::Generic(std::string(scene));
  std::optional<CommandResult> reply;
  const Status submitted = SubmitCommand(test_world.World(), {.tags = {std::string(kEditorTagName)}},
                                         "editor_reload_scene", rfl::Generic(std::move(arguments)),
                                         [&reply](CommandResult result) { reply = std::move(result); });
  if (!submitted) {
    return std::unexpected(submitted.error());
  }
  test_world.Tick();
  return reply.value_or(std::unexpected("no reply"));
}

flecs::entity LocalPlayerEntity(Z13TestWorld& test_world) {
  const auto id = test_world.World().get<z13::gameplay::LocalPlayer>().id;
  return test_world.World().lookup(z13::gameplay::PlayerEntityName(id.value()).c_str());
}

TEST(SceneReloadTest, TheSceneIsBuiltAgainAndThePlayerStaysWhereItStood) {
  Z13TestWorld test_world({std::string(z13::testing::kStationSceneArg), std::string(kScene)});
  test_world.Tick();
  flecs::world world = test_world.World();
  const int blocks = world.count<z13::station::Block>();
  ASSERT_GT(blocks, 0);
  Eigen::Matrix4f moved = LocalPlayerEntity(test_world).get<Eigen::Matrix4f>();
  moved(0, 3) += kMovedBy;
  LocalPlayerEntity(test_world).set<Eigen::Matrix4f>(moved);
  world.entity(kMarkerName.data()).add<z13::flecs_tools::StateEntity>();

  const CommandResult reply = Reload(test_world);
  test_world.Tick();  // Gameplay is added
  test_world.Tick();  // the player is put back

  ASSERT_TRUE(reply) << reply.error();
  EXPECT_TRUE(world.has<z13::gameplay::Gameplay>());
  EXPECT_FALSE(world.lookup(kMarkerName.data()).is_valid()) << "the old scene is gone";
  EXPECT_EQ(world.count<z13::station::Block>(), blocks);
  // Only across the floor: the player settles under gravity after the restart.
  const Eigen::Matrix4f after = LocalPlayerEntity(test_world).get<Eigen::Matrix4f>();
  const Eigen::Vector2f where = after.block<2, 1>(0, 3);
  const Eigen::Vector2f expected = moved.block<2, 1>(0, 3);
  EXPECT_TRUE(where.isApprox(expected, kPoseTolerance));
}

TEST(SceneReloadTest, NothingIsReloadedWithoutAStationGame) {
  Z13TestWorld test_world;
  test_world.Tick();

  const CommandResult reply = Reload(test_world, kScene);

  EXPECT_FALSE(reply);
}

}  // namespace
}  // namespace z13::station_editor
