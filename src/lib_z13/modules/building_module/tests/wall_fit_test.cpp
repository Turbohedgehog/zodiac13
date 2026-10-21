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
#include <vector>

#include <Eigen/Dense>

#include <z13/components/gameplay.h>
#include <z13/components/wall.h>
#include <primitives/wall_panel.h>
#include <z13_settings/building_tuning.h>
#include <z13_tests/shipped_station.h>

#include "../../z13_module/tests/support/z13_test_world.h"
#include "../src/wall_entities.h"
#include "../src/wall_fit.h"
#include "support/station_builders.h"

// Whether a wall can be laid: its panel and the panels it re-cuts, against what stands.
namespace z13::building {
namespace {

using z13::building::primitives::WallGeometry;
using z13::station::WallAxis;
using z13::testing::StationWorld;
using z13::testing::Z13TestWorld;

constexpr int kStickLength = 8;
constexpr int kThickness = 2;
constexpr int kRun = 10;
// Clear of the test station's own blocks.
const Eigen::Vector3i kCorner {8, -4, 0};
const Eigen::Vector3i kUp {0, kRun, 0};
const Eigen::Vector3i kAcross {kRun, 0, 0};

WallGeometry Wall(const Eigen::Vector3i& from, const Eigen::Vector3i& to, int thickness = kThickness) {
  return {.node_a = from, .node_b = to, .stick_axis = WallAxis::kZ, .stick_length = kStickLength,
          .thickness = thickness};
}

void Lay(Z13TestWorld& test_world, const WallGeometry& wall) {
  flecs::world world = test_world.World();
  SpawnWall(world, world.get_mut<z13::gameplay::IdCounters>(), wall);
  test_world.Tick();
  test_world.Tick();
}

BuildView ViewOf(flecs::world world) {
  return {.palette = world.get<z13::building::primitives::BlockPalette>().palette,
          .index = world.get<z13::building::grid::BlockIndex>(),
          .tuning = world.get<BuildingTuning>(),
          .players = {},
          .spawn_clearances = {}};
}

Status Fits(Z13TestWorld& test_world, const WallGeometry& wall) {
  return CheckWallFits(test_world.World(), wall, ViewOf(test_world.World()));
}

TEST(WallFitTest, AWallMeetingAnotherAtACornerFitsOnEitherSide) {
  Z13TestWorld test_world = StationWorld();
  Lay(test_world, Wall(kCorner, kCorner + kUp));

  for (const int thickness : {kThickness, -kThickness}) {
    const Status fits = Fits(test_world, Wall(kCorner, kCorner + kAcross, thickness));
    EXPECT_TRUE(fits.has_value()) << thickness << ": " << (fits ? "" : fits.error());
  }
}

TEST(WallFitTest, ATrapezoidMeetingAnotherAtACornerFitsToo) {
  Z13TestWorld test_world = StationWorld();
  Lay(test_world, Wall(kCorner, kCorner + kUp));
  WallGeometry trapezoid = Wall(kCorner, kCorner + kAcross);
  trapezoid.stick_growth = -kStickLength / 2;

  const Status fits = Fits(test_world, trapezoid);

  EXPECT_TRUE(fits.has_value()) << (fits ? "" : fits.error());
}

TEST(WallFitTest, TheSquareEndOfSuchAWallWouldCollideWithTheOneItJoins) {
  Z13TestWorld test_world = StationWorld();
  Lay(test_world, Wall(kCorner, kCorner + kUp));
  const auto& palette = test_world.World().get<z13::building::primitives::BlockPalette>().palette;

  const auto collides = [&](int thickness) {
    const auto square = z13::building::primitives::WallPanelBlock(Wall(kCorner, kCorner + kAcross, thickness), palette);
    return !ValidateBuild(square.value(), ViewOf(test_world.World())).has_value();
  };
  EXPECT_TRUE(collides(kThickness) || collides(-kThickness));
}

TEST(WallFitTest, AWallCrossingAnotherAwayFromItsNodesIsRefused) {
  Z13TestWorld test_world = StationWorld();
  Lay(test_world, Wall(kCorner, kCorner + kUp));

  EXPECT_FALSE(Fits(test_world, Wall(kCorner + Eigen::Vector3i(-kRun / 2, kRun / 2, 0), kCorner + Eigen::Vector3i(kRun / 2, kRun / 2, 0))).has_value());
}

TEST(WallFitTest, AWallThatBreaksAJointIsRefused) {
  Z13TestWorld test_world = StationWorld();
  Lay(test_world, Wall(kCorner, kCorner + kUp));

  const Status fits = Fits(test_world, Wall(kCorner, kCorner + Eigen::Vector3i(1, kRun, 0)));

  ASSERT_FALSE(fits.has_value());
  EXPECT_NE(fits.error().find("joint"), std::string::npos) << fits.error();
}

}  // namespace
}  // namespace z13::building
