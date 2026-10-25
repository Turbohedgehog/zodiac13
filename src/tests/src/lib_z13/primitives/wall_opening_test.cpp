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
#include <array>
#include <set>
#include <vector>

#include <Eigen/Dense>

#include <primitives/occupancy.h>
#include <primitives/wall_opening.h>
#include <primitives/wall_seam.h>
#include <z13_tests/shipped_station.h>

namespace z13::building::primitives {
namespace {

using z13::station::Block;
using z13::station::OpeningKind;
using z13::station::WallAxis;
using z13::station::WallOpening;

const Palette& ShippedPalette() {
  static const Palette palette = z13::testing::ShippedPalette().value();
  return palette;
}

constexpr int kHeight = 8;
constexpr int kThickness = 2;

WallGeometry Wall(const Eigen::Vector3i& to) {
  return {.node_b = to, .stick_a = {WallAxis::kZ, kHeight}, .stick_b = {WallAxis::kZ, kHeight},
          .thickness = kThickness};
}

WallOpening Door(int offset, int width, int height) {
  return {.id = 1, .segment_id = 1, .kind = OpeningKind::kDoor, .offset = offset, .width = width, .height = height};
}

WallOpening Window(int offset, int width, int sill, int height) {
  return {.id = 2, .segment_id = 1, .kind = OpeningKind::kWindow, .offset = offset, .width = width, .sill = sill,
          .height = height};
}

using Cells = std::set<std::array<int, 3>>;

Cells CellsOf(const Block& block) {
  Cells cells;
  for (const CellBox& box : SealedBoxes(block, ShippedPalette())) {
    for (int x = box.min.x(); x < box.End().x(); ++x) {
      for (int y = box.min.y(); y < box.End().y(); ++y) {
        for (int z = box.min.z(); z < box.End().z(); ++z) {
          cells.insert({x, y, z});
        }
      }
    }
  }
  return cells;
}

std::vector<Block> Panels(
    const WallGeometry& wall, const std::vector<WallOpening>& openings, const WallEnds& ends = {}) {
  return OpeningPanels(wall, ends, openings, ShippedPalette()).value();
}

TEST(WallOpeningTest, AStepIsTheWallsOffsetOverItsGcd) {
  EXPECT_EQ(WallSteps(Wall({12, 0, 0})), 12);
  EXPECT_EQ(WallSteps(Wall({8, 4, 0})), 4);
  EXPECT_EQ(WallSteps(Wall({7, 3, 0})), 1);
  EXPECT_EQ(WallSteps(Wall({0, 0, 5})), 0) << "the stick's own axis does not count";
}

TEST(WallOpeningTest, ADoorLeavesTheWallOnEachSideAndAboveAndFillsItsGapWithALeaf) {
  const std::vector<Block> panels = Panels(Wall({12, 0, 0}), {Door(4, 4, 6)});
  EXPECT_EQ(panels.size(), 4u);
  EXPECT_EQ(std::ranges::count(panels, z13::station::kDoorPanelPrimitiveId, [](const Block& block) {
              return block.spec.type_id;
            }),
            1);
}

TEST(WallOpeningTest, AWindowAddsASillAndAPane) {
  const std::vector<Block> panels = Panels(Wall({12, 0, 0}), {Window(4, 4, 2, 3)});
  ASSERT_EQ(panels.size(), 5u);
  EXPECT_EQ(std::ranges::count(panels, z13::station::kGlassPanelPrimitiveId, [](const Block& block) {
              return block.spec.type_id;
            }),
            1);
}

// The pieces and the pane tile the wall: no cell twice, none left out.
void ExpectTilesTheWall(
    const WallGeometry& wall, const std::vector<WallOpening>& openings, const WallEnds& ends = {}) {
  Cells union_of_pieces;
  size_t total = 0;
  for (const Block& block : Panels(wall, openings, ends)) {
    const Cells cells = CellsOf(block);
    total += cells.size();
    union_of_pieces.insert(cells.begin(), cells.end());
  }
  const Cells whole = CellsOf(WallPanelBlock(wall, ShippedPalette(), ends).value());
  EXPECT_EQ(total, union_of_pieces.size()) << "a cell in two pieces";
  EXPECT_EQ(union_of_pieces, whole);
}

TEST(WallOpeningTest, AWindowInAnAxisWallLeavesNoGap) {
  ExpectTilesTheWall(Wall({12, 0, 0}), {Window(4, 4, 2, 3)});
}

TEST(WallOpeningTest, AWindowInAnObliqueWallLeavesNoGap) {
  ExpectTilesTheWall(Wall({8, 4, 0}), {Window(1, 2, 1, 4)});
  ExpectTilesTheWall(Wall({-9, 6, 0}), {Window(1, 1, 2, 3)});
}

TEST(WallOpeningTest, ADoorInAnObliqueWallLeavesNoGapEither) {
  ExpectTilesTheWall(Wall({8, 4, 0}), {Door(1, 2, 5)});
  ExpectTilesTheWall(Wall({-9, 6, 0}), {Door(1, 1, 4)});
}

TEST(WallOpeningTest, ADoorInAnObliqueWallOpensTheCellsBetweenTheJambs) {
  const WallGeometry wall = Wall({8, 4, 0});
  const Cells whole = CellsOf(WallPanelBlock(wall, ShippedPalette()).value());
  Cells kept;
  for (const Block& block : Panels(wall, {Door(1, 2, 5)})) {
    if (block.spec.type_id == z13::station::kDoorPanelPrimitiveId) {
      continue;
    }
    const Cells cells = CellsOf(block);
    kept.insert(cells.begin(), cells.end());
  }
  EXPECT_LT(kept.size(), whole.size());
  EXPECT_TRUE(std::ranges::all_of(kept, [&](const std::array<int, 3>& cell) { return whole.contains(cell); }));
  const auto missing = std::ranges::count_if(whole, [&](const std::array<int, 3>& cell) {
    return !kept.contains(cell);
  });
  EXPECT_GT(missing, 0);
  EXPECT_TRUE(std::ranges::all_of(whole, [&](const std::array<int, 3>& cell) {
    return kept.contains(cell) || cell[2] < 5;
  })) << "a cell above the door's height is gone";
}

TEST(WallOpeningTest, TwoOpeningsInOneWall) {
  WallOpening second = Door(8, 2, 5);
  second.id = 3;
  EXPECT_EQ(Panels(Wall({12, 0, 0}), {Door(2, 2, 5), second}).size(), 7u);
  ExpectTilesTheWall(Wall({12, 0, 0}), {Window(2, 2, 2, 3), Window(8, 2, 2, 3)});
}

TEST(WallOpeningTest, OpeningsTheWallCannotHoldAreRefused) {
  const WallGeometry wall = Wall({12, 0, 0});
  const auto refused = [&](const std::vector<WallOpening>& openings) {
    return !OpeningPanels(wall, {}, openings, ShippedPalette()).has_value();
  };
  EXPECT_TRUE(refused({Door(0, 4, 5)})) << "no wall left at the start";
  EXPECT_TRUE(refused({Door(8, 4, 5)})) << "no wall left at the end";
  EXPECT_TRUE(refused({Door(4, 4, kHeight)})) << "no wall above";
  EXPECT_TRUE(refused({Door(4, 0, 5)}));
  EXPECT_TRUE(refused({Door(4, 4, 0)}));
  WallOpening raised = Door(4, 4, 3);
  raised.sill = 1;
  EXPECT_TRUE(refused({raised})) << "a door reaches the floor";
  WallOpening crowding = Door(5, 4, 5);
  crowding.id = 3;
  EXPECT_TRUE(refused({Door(4, 4, 5), crowding})) << "overlapping";
  EXPECT_FALSE(refused({Door(4, 4, 5)}));
}

TEST(WallOpeningTest, ABentWallTakesNoOpenings) {
  WallGeometry bent = Wall({12, 0, 0});
  bent.stick_b = {WallAxis::kY, kHeight};
  EXPECT_FALSE(OpeningPanels(bent, {}, {{Door(4, 4, 5)}}, ShippedPalette()).has_value());
}

WallGeometry Trapezoid(const Eigen::Vector3i& to, int far_length) {
  WallGeometry wall = Wall(to);
  wall.stick_b.length = far_length;
  return wall;
}

// The pieces of a trapezoid share no cell, and the cells of a column meet without a gap.
void ExpectPiecesMeet(const WallGeometry& wall, const std::vector<WallOpening>& openings) {
  std::set<std::array<int, 3>> seen;
  for (const Block& block : Panels(wall, openings)) {
    for (const auto& cell : CellsOf(block)) {
      EXPECT_TRUE(seen.insert(cell).second) << "a cell in two pieces";
    }
  }
  for (const auto& cell : seen) {
    const std::array<int, 3> above {cell[0], cell[1], cell[2] + 1};
    const std::array<int, 3> two_above {cell[0], cell[1], cell[2] + 2};
    EXPECT_TRUE(seen.contains(above) || !seen.contains(two_above)) << "a gap in a column";
  }
}

TEST(WallOpeningTest, ADoorInATrapezoidKeepsTheBrokenTopOfTheWall) {
  const WallGeometry wall = Trapezoid({12, 0, 0}, kHeight - 4);
  EXPECT_EQ(Panels(wall, {Door(4, 4, 3)}).size(), 4u);
  ExpectPiecesMeet(wall, {Door(4, 4, 3)});
}

TEST(WallOpeningTest, AWindowInAnObliqueTrapezoidLeavesNoGap) {
  ExpectPiecesMeet(Trapezoid({8, 4, 0}, kHeight - 3), {Window(1, 2, 1, 3)});
}

TEST(WallOpeningTest, AnOpeningInATrapezoidKeepsACellOfWallAboveItsLowerSide) {
  const WallGeometry wall = Trapezoid({12, 0, 0}, 4);
  EXPECT_FALSE(OpeningPanels(wall, {}, {{Door(8, 3, 4)}}, ShippedPalette()).has_value()) << "the low side is under 5";
  EXPECT_TRUE(OpeningPanels(wall, {}, {{Door(2, 3, 5)}}, ShippedPalette()).has_value());
}

void ExpectSeamedWallTilesWithAnOpening(const WallGeometry& wall, const WallGeometry& other, const WallOpening& opening) {
  constexpr int kReach = 16;
  const auto seam = PlanSeam(PlanWallAt(other, true).value(), PlanWallAt(wall, true).value(), kReach);
  ASSERT_TRUE(seam.has_value()) << seam.error();
  ExpectTilesTheWall(wall, {opening}, {.at_a = *seam});
}

TEST(WallOpeningTest, AWallSeamedAtAnAngleTakesAnOpening) {
  const WallGeometry east {
      .node_b = {12, 0, 0}, .stick_a = {WallAxis::kZ, kHeight}, .stick_b = {WallAxis::kZ, kHeight}, .thickness = 2};
  ExpectSeamedWallTilesWithAnOpening(Wall({8, 8, 0}), east, Door(3, 3, 5));
  ExpectSeamedWallTilesWithAnOpening(Wall({6, 12, 0}), east, Window(2, 2, 1, 3));
}

}  // namespace
}  // namespace z13::building::primitives
