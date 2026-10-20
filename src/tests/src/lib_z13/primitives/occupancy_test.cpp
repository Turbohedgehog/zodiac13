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

#include <unordered_set>
#include <vector>

#include <z13_tests/columns.h>
#include <z13_tests/shipped_station.h>

#include <primitives/occupancy.h>

namespace z13::building::primitives {
namespace {

using z13::station::Block;
using z13::station::Orientation;

constexpr uint32_t kSlopeId = 5;
constexpr uint32_t kWallId = 2;

// The slope's surface is higher than the cell's floor somewhere over the cell.
bool ReachedBySlope(const Eigen::Vector3i& cell, const Eigen::Vector3i& size) {
  return cell.z() * size.x() < (cell.x() + 1) * size.z();
}

CellShape ShapeOfSlope(const Eigen::Vector3i& size, Orientation orientation = Orientation::kFacePosXUpPosZ,
                       const Eigen::Vector3i& cell = Eigen::Vector3i::Zero()) {
  const Palette palette = z13::testing::ShippedPalette().value();
  return ShapeOfBlock({.spec = {.type_id = kSlopeId, .size = size, .orientation = orientation}, .cell = cell}, palette);
}

TEST(OccupancyTest, ABoxFillsItsWholeBox) {
  const Palette palette = z13::testing::ShippedPalette().value();
  const CellShape shape = ShapeOfBlock({.spec = {.type_id = kWallId, .size = {8, 1, 8}}}, palette);

  EXPECT_FALSE(shape.filled.has_value());
  EXPECT_EQ(shape.box.extent, Eigen::Vector3i(8, 1, 8));
}

TEST(OccupancyTest, ASlopeFillsOnlyTheCellsItsSurfaceReaches) {
  for (const Eigen::Vector3i size : {Eigen::Vector3i(4, 4, 4), Eigen::Vector3i(8, 2, 2), Eigen::Vector3i(16, 3, 4)}) {
    const CellShape shape = ShapeOfSlope(size);
    ASSERT_TRUE(shape.filled.has_value());
    for (int z = 0; z < size.z(); ++z) {
      for (int y = 0; y < size.y(); ++y) {
        for (int x = 0; x < size.x(); ++x) {
          const Eigen::Vector3i cell(x, y, z);
          EXPECT_EQ(shape.Contains(cell), ReachedBySlope(cell, size))
              << "cell " << cell.transpose() << " of " << size.transpose();
        }
      }
    }
  }
}

// A cell the slope only touches along its surface stays free.
TEST(OccupancyTest, TheEmptyHalfOfASlopeIsFree) {
  const CellShape shape = ShapeOfSlope({4, 4, 4});

  EXPECT_TRUE(shape.Contains({3, 0, 3}));
  EXPECT_FALSE(shape.Contains({0, 0, 3}));
  EXPECT_FALSE(shape.Overlaps(CellBox {.min = {0, 0, 1}, .extent = {1, 4, 3}}));
  EXPECT_TRUE(shape.Overlaps(CellBox {.min = {0, 0, 0}, .extent = {1, 4, 1}}));
}

// Each world cell's centre, taken back into the primitive's frame, must be a filled cell there.
TEST(OccupancyTest, ATurnedSlopeFillsTheTurnedCells) {
  const Eigen::Vector3i size(8, 2, 4);
  const Eigen::Vector3i cell(-5, 3, 7);
  for (const Orientation orientation : kOrientations) {
    const Block block {.spec = {.type_id = kSlopeId, .size = size, .orientation = orientation}, .cell = cell};
    const CellShape shape = ShapeOfSlope(size, orientation, cell);
    const CellPose pose = PoseOf(block);
    const CellBox box = OccupiedCells(block);
    ASSERT_EQ(shape.box, box);
    for (int z = box.min.z(); z < box.End().z(); ++z) {
      for (int y = box.min.y(); y < box.End().y(); ++y) {
        for (int x = box.min.x(); x < box.End().x(); ++x) {
          const Eigen::Vector3f centre = Eigen::Vector3f(x, y, z) + Eigen::Vector3f::Constant(0.5f);
          const Eigen::Vector3f own =
              pose.rotation.transpose().cast<float>() * (centre - pose.origin.cast<float>());
          const Eigen::Vector3i own_cell = own.array().floor().cast<int>();
          EXPECT_EQ(shape.Contains({x, y, z}), ReachedBySlope(own_cell, size))
              << "orientation " << static_cast<int>(orientation) << " cell " << x << ' ' << y << ' ' << z;
        }
      }
    }
  }
}

// No gap for a ball falling through: every column of the run has a filled cell at its top.
TEST(OccupancyTest, EveryColumnOfASlopeHasAFilledTop) {
  const Eigen::Vector3i size(16, 3, 4);
  const CellShape shape = ShapeOfSlope(size);
  for (int y = 0; y < size.y(); ++y) {
    for (int x = 0; x < size.x(); ++x) {
      EXPECT_TRUE(shape.Contains({x, y, (x * size.z()) / size.x()})) << x << ' ' << y;
    }
  }
}

TEST(OccupancyTest, ACornerSlopeFillsItsApexColumn) {
  const Palette palette = z13::testing::ShippedPalette().value();
  const auto corner = palette.Find("CornerSlope");
  ASSERT_TRUE(corner.has_value());
  const CellShape shape = ShapeOfBlock({.spec = {.type_id = corner->get().id, .size = {4, 4, 4}}}, palette);

  ASSERT_TRUE(shape.filled.has_value());
  EXPECT_TRUE(shape.Contains({3, 3, 3}));
  EXPECT_TRUE(shape.Contains({0, 0, 0}));
  EXPECT_FALSE(shape.Contains({0, 0, 3}));
}

using ColumnSet = std::unordered_set<Eigen::Vector2i, z13::testing::ColumnHash>;

// The panel's period, laid end to end n times rising by `slant` each: a wall of that direction.
ColumnSet SealedCellsOfWall(int run, int slant, int thickness, int periods) {
  const Palette palette = z13::testing::ShippedPalette().value();
  ColumnSet sealed;
  for (int k = 0; k < periods; ++k) {
    const Block period {
        .spec = {.type_id = z13::station::kPanelPrimitiveId, .size = {run, 1, slant + thickness}, .slant = slant},
        .cell = {k * run, 0, k * slant}};
    for (const CellBox& box : SealedBoxes(period, palette)) {
      for (int x = box.min.x(); x < box.End().x(); ++x) {
        for (int z = box.min.z(); z < box.End().z(); ++z) {
          sealed.emplace(x, z);
        }
      }
    }
  }
  return sealed;
}

// Air at the wall's high end can't reach air at its low end within the wall's own span.
bool Leaks(const ColumnSet& sealed, int width, int height) {
  ColumnSet seen;
  std::vector<Eigen::Vector2i> open {{0, height - 1}};
  while (!open.empty()) {
    const Eigen::Vector2i column = open.back();
    open.pop_back();
    const int x = column.x();
    const int z = column.y();
    if (x < 0 || z < 0 || x >= width || z >= height || sealed.contains(column) || !seen.insert(column).second) {
      continue;
    }
    open.insert(open.end(), {{x + 1, z}, {x - 1, z}, {x, z + 1}, {x, z - 1}});
  }
  return seen.contains(Eigen::Vector2i {width - 1, 0});
}

TEST(OccupancyTest, AMinimalPanelOfAnyDirectionSealsAgainstAir) {
  constexpr int kPeriods = 3;
  const Palette palette = z13::testing::ShippedPalette().value();
  const Shape& panel = palette.Find(z13::station::kPanelPrimitiveId)->get().shape;
  for (const int run : {1, 2, 3, 5, 7, 13}) {
    for (const int slant : {0, 1, 2, 3, 4, 7, 11}) {
      int thickness = 1;
      while (!panel.AllowsSize({run, 1, slant + thickness}, slant)) {
        ++thickness;
      }
      const auto sealed = SealedCellsOfWall(run, slant, thickness, kPeriods);
      const int height = (kPeriods - 1) * slant + thickness + slant;
      if (slant > 0) {
        EXPECT_FALSE(Leaks(sealed, kPeriods * run, height)) << run << ':' << slant << " thickness " << thickness;
      }
    }
  }
}

TEST(OccupancyTest, ATooThinDiagonalPanelIsNotAllowed) {
  const Palette palette = z13::testing::ShippedPalette().value();
  const Shape& panel = palette.Find(z13::station::kPanelPrimitiveId)->get().shape;

  EXPECT_FALSE(panel.AllowsSize({4, 1, 4 + 1}, 4));
  EXPECT_TRUE(panel.AllowsSize({4, 1, 4 + 2}, 4));
  EXPECT_FALSE(panel.AllowsSize({4, 1, 3}, 4)) << "no thickness left";
  EXPECT_FALSE(panel.AllowsSize({4, 1, 4}, -1));
  EXPECT_FALSE(palette.Find(kWallId)->get().shape.AllowsSize({4, 1, 4}, 1)) << "only a panel slants";
}

}  // namespace
}  // namespace z13::building::primitives
