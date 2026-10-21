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
#include <cstdlib>
#include <deque>
#include <set>
#include <vector>

#include <Eigen/Dense>

#include <primitives/geometry.h>
#include <primitives/occupancy.h>
#include <primitives/placement.h>
#include <primitives/triangle_panel.h>
#include <z13_tests/shipped_station.h>

namespace z13::building::primitives {
namespace {

using z13::station::Block;

const Palette& ShippedPalette() {
  static const Palette palette = z13::testing::ShippedPalette().value();
  return palette;
}

const Triangle kFloor {Eigen::Vector3i {0, 0, 0}, {6, 0, 0}, {0, 6, 0}};
const Triangle kWall {Eigen::Vector3i {0, 0, 0}, {6, 0, 0}, {0, 0, 5}};
const Triangle kSlope {Eigen::Vector3i {0, 0, 0}, {4, 0, 2}, {0, 4, 0}};
const Triangle kOblique {Eigen::Vector3i {0, 0, 0}, {5, 3, 1}, {2, 6, 4}};
const Triangle kSteepOblique {Eigen::Vector3i {0, 0, 0}, {-3, 5, 6}, {4, 2, -5}};
const Eigen::Vector3i kShift {-10, 7, 3};

Triangle Shifted(const Triangle& triangle) {
  return {triangle[0] + kShift, triangle[1] + kShift, triangle[2] + kShift};
}

std::vector<Triangle> Triangles() {
  std::vector<Triangle> triangles {kFloor, kWall, kSlope, kOblique, kSteepOblique};
  for (const Triangle& triangle : std::vector<Triangle>(triangles)) {
    triangles.push_back(Shifted(triangle));
  }
  return triangles;
}

struct LessLexicographically {
  bool operator()(const Eigen::Vector3i& a, const Eigen::Vector3i& b) const {
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end());
  }
};

using Corners = std::set<Eigen::Vector3i, LessLexicographically>;

Corners PlacedCorners(const Block& block) {
  const CellPose pose = PoseOf(block);
  Corners placed;
  for (const ConvexSolid& solid : BlockSolids(ShippedPalette(), ShapeKeyOf(block.spec))) {
    for (const Eigen::Vector3f& vertex : solid.vertices) {
      placed.insert(pose.origin + pose.rotation * vertex.array().round().cast<int>().matrix());
    }
  }
  return placed;
}

TEST(TrianglePanelTest, ATriangleThickensAlongTheAxisNearestItsNormal) {
  EXPECT_EQ(ThickeningAxisOf(kFloor), 2);
  EXPECT_EQ(ThickeningAxisOf(kWall), 1);
  EXPECT_EQ(ThickeningAxisOf(kSlope), 2);
  EXPECT_EQ(ThickeningAxisOf({Eigen::Vector3i {0, 0, 0}, {5, 0, 0}, {0, 5, 0}}), 2);
  EXPECT_EQ(ThickeningAxisOf({Eigen::Vector3i {5, 0, 0}, {0, 5, 0}, {0, 0, 5}}), 0) << "a tie takes the lower axis";
  EXPECT_FALSE(ThickeningAxisOf({Eigen::Vector3i {0, 0, 0}, {1, 1, 1}, {2, 2, 2}}).has_value());
}

TEST(TrianglePanelTest, ASlopedTriangleNeedsMoreThanOneCell) {
  EXPECT_EQ(MinTriangleThickness(kFloor), 1);
  EXPECT_EQ(MinTriangleThickness(kWall), 1);
  EXPECT_EQ(MinTriangleThickness(kSlope), 2);
  EXPECT_FALSE(TrianglePanelBlock(kSlope, 1, ShippedPalette()).has_value());
  EXPECT_FALSE(TrianglePanelBlock(kSlope, 0, ShippedPalette()).has_value());
  EXPECT_TRUE(TrianglePanelBlock(kSlope, 2, ShippedPalette()).has_value());
  EXPECT_TRUE(TrianglePanelBlock(kSlope, -2, ShippedPalette()).has_value());
}

TEST(TrianglePanelTest, CornersOnOneLineMakeNoWall) {
  const Triangle line {Eigen::Vector3i {0, 0, 0}, {1, 2, 3}, {2, 4, 6}};

  EXPECT_FALSE(TrianglePanelBlock(line, 3, ShippedPalette()).has_value());
}

TEST(TrianglePanelTest, TheBlockIsTheTriangleAndItsCopyOneThicknessAway) {
  for (const Triangle& triangle : Triangles()) {
    const int axis = ThickeningAxisOf(triangle).value();
    const int least = MinTriangleThickness(triangle).value();
    for (const int thickness : {least, -least, least + 1}) {
      const auto block = TrianglePanelBlock(triangle, thickness, ShippedPalette());
      ASSERT_TRUE(block.has_value()) << block.error();
      Corners expected;
      for (const Eigen::Vector3i& corner : triangle) {
        Eigen::Vector3i other = corner;
        other[axis] += thickness;
        expected.insert(corner);
        expected.insert(other);
      }
      EXPECT_EQ(PlacedCorners(*block), expected) << axis << ' ' << thickness;
    }
  }
}

TEST(TrianglePanelTest, EverySealedCellIsInsideTheShape) {
  for (const Triangle& triangle : Triangles()) {
    const Block block = TrianglePanelBlock(triangle, MinTriangleThickness(triangle).value(), ShippedPalette()).value();
    const CellShape shape = ShapeOfBlock(block, ShippedPalette());
    for (const CellBox& box : SealedBoxes(block, ShippedPalette())) {
      for (int x = 0; x < box.extent.x(); ++x) {
        for (int y = 0; y < box.extent.y(); ++y) {
          for (int z = 0; z < box.extent.z(); ++z) {
            EXPECT_TRUE(shape.Contains(box.min + Eigen::Vector3i(x, y, z)));
          }
        }
      }
    }
  }
}

// Whether the point (doubled to stay whole) is inside the triangle seen along `axis`.
bool CentreInsideProjection(const Triangle& triangle, int axis, const Eigen::Vector2i& cell) {
  const int x_axis = axis == 0 ? 1 : 0;
  const int z_axis = axis == 2 ? 1 : 2;
  const auto project = [&](const Eigen::Vector3i& corner) {
    return Eigen::Vector2i(2 * corner[x_axis], 2 * corner[z_axis]);
  };
  const Eigen::Vector2i centre(2 * cell.x() + 1, 2 * cell.y() + 1);
  int sign = 0;
  for (size_t i = 0; i < triangle.size(); ++i) {
    const Eigen::Vector2i from = project(triangle[i]);
    const Eigen::Vector2i to = project(triangle[(i + 1) % triangle.size()]);
    const Eigen::Vector2i edge = to - from;
    const Eigen::Vector2i point = centre - from;
    const int cross = edge.x() * point.y() - edge.y() * point.x();
    if (cross == 0) {
      return false;
    }
    if (sign != 0 && (cross > 0) != (sign > 0)) {
      return false;
    }
    sign = cross > 0 ? 1 : -1;
  }
  return true;
}

TEST(TrianglePanelTest, AirDoesNotPassThroughTheMiddleOfAWallOfTheLeastThickness) {
  for (const Triangle& triangle : Triangles()) {
    const int axis = ThickeningAxisOf(triangle).value();
    const int x_axis = axis == 0 ? 1 : 0;
    const int z_axis = axis == 2 ? 1 : 2;
    const Block block = TrianglePanelBlock(triangle, MinTriangleThickness(triangle).value(), ShippedPalette()).value();
    const CellBox cells = OccupiedCells(block);
    const std::vector<CellBox> sealed = SealedBoxes(block, ShippedPalette());
    const auto is_sealed = [&sealed](const Eigen::Vector3i& cell) {
      return std::ranges::any_of(sealed, [&cell](const CellBox& box) { return box.Contains(cell); });
    };
    constexpr int kMargin = 2;
    const int bottom = cells.min[axis] - kMargin;
    const int top = cells.End()[axis] + kMargin;

    // Columns whose centre lies inside the triangle seen along the axis; air may only move among them.
    std::vector<Eigen::Vector2i> columns;
    for (int x = cells.min[x_axis]; x < cells.End()[x_axis]; ++x) {
      for (int z = cells.min[z_axis]; z < cells.End()[z_axis]; ++z) {
        if (CentreInsideProjection(triangle, axis, {x, z})) {
          columns.emplace_back(x, z);
        }
      }
    }
    ASSERT_FALSE(columns.empty());
    const auto cell_at = [&](const Eigen::Vector2i& column, int level) {
      Eigen::Vector3i cell;
      cell[x_axis] = column.x();
      cell[z_axis] = column.y();
      cell[axis] = level;
      return cell;
    };
    std::set<Eigen::Vector3i, LessLexicographically> seen;
    std::deque<Eigen::Vector3i> queue;
    for (const Eigen::Vector2i& column : columns) {
      queue.push_back(cell_at(column, bottom));
      seen.insert(queue.back());
    }
    bool reached_top = false;
    while (!queue.empty()) {
      const Eigen::Vector3i cell = queue.front();
      queue.pop_front();
      reached_top = reached_top || cell[axis] == top;
      for (int step_axis = 0; step_axis < 3; ++step_axis) {
        for (const int step : {-1, 1}) {
          Eigen::Vector3i next = cell;
          next[step_axis] += step;
          const Eigen::Vector2i column(next[x_axis], next[z_axis]);
          const bool in_columns = std::ranges::find(columns, column) != columns.end();
          if (!in_columns || next[axis] < bottom || next[axis] > top || is_sealed(next) || !seen.insert(next).second) {
            continue;
          }
          queue.push_back(next);
        }
      }
    }
    EXPECT_FALSE(reached_top) << "axis " << axis;
  }
}

}  // namespace
}  // namespace z13::building::primitives
