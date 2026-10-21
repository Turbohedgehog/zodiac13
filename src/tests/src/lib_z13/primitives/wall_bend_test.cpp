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
#include <cmath>
#include <limits>
#include <optional>
#include <vector>

#include <Eigen/Dense>

#include <primitives/geometry.h>
#include <primitives/occupancy.h>
#include <primitives/placement.h>
#include <primitives/triangle_panel.h>
#include <primitives/wall_bend.h>
#include <z13_tests/shipped_station.h>

namespace z13::building::primitives {
namespace {

using z13::station::Block;
using z13::station::WallAxis;
using z13::station::WallDiagonal;

constexpr double kEpsilon = 1e-9;

const Palette& ShippedPalette() {
  static const Palette palette = z13::testing::ShippedPalette().value();
  return palette;
}

// Stick along Z at the origin, along Y at the other node: the surface between them is bent.
WallGeometry Bent(int thickness, WallDiagonal diagonal) {
  return {.node_b = {10, 0, 0}, .stick_a = {WallAxis::kZ, 6}, .stick_b = {WallAxis::kY, 5}, .thickness = thickness,
          .diagonal = diagonal};
}

TEST(WallBendTest, OnlySticksOfDifferentAxesBendAWall) {
  WallGeometry wall = Bent(2, WallDiagonal::kBaseToTip);
  EXPECT_TRUE(IsBent(wall));
  wall.stick_b = {WallAxis::kZ, 4};
  EXPECT_FALSE(IsBent(wall));
}

TEST(WallBendTest, TheDiagonalPicksTheTwoTrianglesOfTheCorners) {
  const WallGeometry wall = Bent(2, WallDiagonal::kBaseToTip);
  const Eigen::Vector3i a_base {0, 0, 0};
  const Eigen::Vector3i a_tip {0, 0, 6};
  const Eigen::Vector3i b_base {10, 0, 0};
  const Eigen::Vector3i b_tip {10, 5, 0};

  EXPECT_EQ(BentTriangles(wall, WallDiagonal::kBaseToTip),
            (std::array<Triangle, 2> {Triangle {a_base, a_tip, b_tip}, Triangle {a_base, b_tip, b_base}}));
  EXPECT_EQ(BentTriangles(wall, WallDiagonal::kTipToBase),
            (std::array<Triangle, 2> {Triangle {a_base, a_tip, b_base}, Triangle {a_tip, b_tip, b_base}}));
}

// The angle between the normals of the two triangles on either side of `diagonal`.
double FoldOf(const WallGeometry& wall, WallDiagonal diagonal) {
  const std::array<Triangle, 2> triangles = BentTriangles(wall, diagonal);
  const Eigen::Vector3d first = TriangleNormal(triangles[0]).cast<double>().normalized();
  const Eigen::Vector3d second = TriangleNormal(triangles[1]).cast<double>().normalized();
  return std::acos(std::clamp(first.dot(second), -1.0, 1.0));
}

TEST(WallBendTest, TheDefaultDiagonalFoldsTheSurfaceLeast) {
  for (const Eigen::Vector3i& node_b :
       {Eigen::Vector3i(10, 0, 0), Eigen::Vector3i(7, 2, 3), Eigen::Vector3i(-6, 4, -2), Eigen::Vector3i(3, -5, 4)}) {
    WallGeometry wall = Bent(2, WallDiagonal::kBaseToTip);
    wall.node_b = node_b;
    const WallDiagonal chosen = DefaultDiagonal(wall);
    const WallDiagonal other =
        chosen == WallDiagonal::kBaseToTip ? WallDiagonal::kTipToBase : WallDiagonal::kBaseToTip;
    EXPECT_LE(FoldOf(wall, chosen), FoldOf(wall, other) + kEpsilon) << node_b.transpose();
  }
}

TEST(WallBendTest, ADiagonalOutsideAFlatQuadIsNotChosen) {
  // The corners lie in one plane, and the base-to-tip diagonal runs outside the quad.
  WallGeometry wall = Bent(2, WallDiagonal::kBaseToTip);
  wall.node_b = {10, 0, 3};
  wall.stick_b = {WallAxis::kX, 4};

  EXPECT_EQ(DefaultDiagonal(wall), WallDiagonal::kTipToBase);
}

TEST(WallBendTest, ABentWallIsTwoTrianglePanelsThickenedTowardOneSide) {
  for (const WallDiagonal diagonal : {WallDiagonal::kBaseToTip, WallDiagonal::kTipToBase}) {
    for (const int thickness : {2, -2}) {
      const WallGeometry wall = Bent(thickness, diagonal);
      const auto blocks = WallPanelBlocks(wall, ShippedPalette());
      ASSERT_TRUE(blocks.has_value()) << blocks.error();
      ASSERT_EQ(blocks->size(), 2u);
      const std::array<Triangle, 2> triangles = BentTriangles(wall, diagonal);
      for (size_t i = 0; i < blocks->size(); ++i) {
        const CellPose pose = PoseOf((*blocks)[i]);
        Eigen::Vector3d centre = Eigen::Vector3d::Zero();
        int count = 0;
        for (const ConvexSolid& solid : BlockSolids(ShippedPalette(), ShapeKeyOf((*blocks)[i].spec))) {
          for (const Eigen::Vector3f& vertex : solid.vertices) {
            centre += (pose.origin + pose.rotation * vertex.array().round().cast<int>().matrix()).cast<double>();
            ++count;
          }
        }
        centre /= count;
        const Eigen::Vector3d middle = (triangles[i][0] + triangles[i][1] + triangles[i][2]).cast<double>() / 3.0;
        const double side = TriangleNormal(triangles[i]).cast<double>().dot(centre - middle);
        EXPECT_EQ(side > 0.0, thickness > 0) << i << ' ' << thickness;
      }
    }
  }
}

TEST(WallBendTest, AParallelWallIsOnePanel) {
  WallGeometry wall = Bent(2, WallDiagonal::kBaseToTip);
  wall.stick_b = {WallAxis::kZ, 6};

  const auto blocks = WallPanelBlocks(wall, ShippedPalette());

  ASSERT_TRUE(blocks.has_value()) << blocks.error();
  EXPECT_EQ(blocks->size(), 1u);
}

TEST(WallBendTest, ABentWallMustBeThickEnoughForItsSteeperTriangle) {
  const WallGeometry thin = Bent(1, WallDiagonal::kBaseToTip);
  const int least = MinWallThickness(thin).value();

  EXPECT_GT(least, 1);
  EXPECT_FALSE(WallPanelBlocks(thin, ShippedPalette()).has_value());
  EXPECT_TRUE(WallPanelBlocks(Bent(least, WallDiagonal::kBaseToTip), ShippedPalette()).has_value());
}

TEST(WallBendTest, TwoSticksOnOneLineMakeNoTriangle) {
  WallGeometry wall = Bent(2, WallDiagonal::kBaseToTip);
  wall.stick_b = {WallAxis::kX, 4};

  EXPECT_FALSE(WallPanelBlocks(wall, ShippedPalette()).has_value());
}

// The closed segment p-q crosses the triangle in its interior, or along `diagonal_edge` (a pair
// of its corners) away from the ends; the crossing point comes back.
std::optional<Eigen::Vector3d> Crossing(
    const Eigen::Vector3d& p, const Eigen::Vector3d& q, const Triangle& triangle,
    const std::array<size_t, 2>& diagonal_edge) {
  const Eigen::Vector3d origin = triangle[0].cast<double>();
  const Eigen::Vector3d normal = TriangleNormal(triangle).cast<double>();
  const double from = normal.dot(p - origin);
  const double to = normal.dot(q - origin);
  if (from * to > 0.0 || from == to) {
    return std::nullopt;
  }
  const Eigen::Vector3d point = p + (q - p) * (from / (from - to));
  const Eigen::Vector3d u = triangle[1].cast<double>() - origin;
  const Eigen::Vector3d v = triangle[2].cast<double>() - origin;
  const Eigen::Vector3d w = point - origin;
  const double uu = u.dot(u);
  const double uv = u.dot(v);
  const double vv = v.dot(v);
  const double determinant = uu * vv - uv * uv;
  const double b = (vv * w.dot(u) - uv * w.dot(v)) / determinant;
  const double c = (uu * w.dot(v) - uv * w.dot(u)) / determinant;
  const std::array<double, 3> weights {1.0 - b - c, b, c};
  if (std::ranges::any_of(weights, [](double weight) { return weight < -kEpsilon; })) {
    return std::nullopt;
  }
  const bool interior = std::ranges::all_of(weights, [](double weight) { return weight > kEpsilon; });
  const auto on_diagonal = [&] {
    for (size_t zero = 0; zero < weights.size(); ++zero) {
      if (std::abs(weights[zero]) <= kEpsilon && zero != diagonal_edge[0] && zero != diagonal_edge[1]) {
        return true;
      }
    }
    return false;
  };
  return interior || on_diagonal() ? std::optional(point) : std::nullopt;
}

double DistanceToSegment(const Eigen::Vector3d& point, const Eigen::Vector3i& from, const Eigen::Vector3i& to) {
  const Eigen::Vector3d start = from.cast<double>();
  const Eigen::Vector3d edge = to.cast<double>() - start;
  const double along = std::clamp((point - start).dot(edge) / edge.squaredNorm(), 0.0, 1.0);
  return (point - (start + edge * along)).norm();
}

struct BendCase {
  Eigen::Vector3i node_b;
  z13::station::WallStick stick_a;
  z13::station::WallStick stick_b;
};

// The corner pair of each of the two triangles that the diagonal joins.
std::array<std::array<size_t, 2>, 2> DiagonalEdges(WallDiagonal diagonal) {
  if (diagonal == WallDiagonal::kBaseToTip) {
    return {{{0, 2}, {0, 1}}};
  }
  return {{{1, 2}, {0, 2}}};
}

TEST(WallBendTest, AirDoesNotPassTheFoldOrTheMiddleOfABentWall) {
  constexpr double kAwayFromTheEdges = 2.0;
  const std::vector<BendCase> cases {
      {{10, 0, 0}, {WallAxis::kZ, 6}, {WallAxis::kY, 5}},
      {{7, 2, 3}, {WallAxis::kX, -5}, {WallAxis::kZ, 6}},
      {{5, 4, -3}, {WallAxis::kY, 4}, {WallAxis::kX, 2}},
      {{4, -2, 3}, {WallAxis::kX, 6}, {WallAxis::kZ, -3}},
  };
  for (const BendCase& test : cases) {
    for (const WallDiagonal diagonal : {WallDiagonal::kBaseToTip, WallDiagonal::kTipToBase}) {
      for (const int sign : {1, -1}) {
        WallGeometry wall {
            .node_b = test.node_b, .stick_a = test.stick_a, .stick_b = test.stick_b, .diagonal = diagonal};
        wall.thickness = sign * MinWallThickness(wall).value();
        const auto blocks = WallPanelBlocks(wall, ShippedPalette());
        ASSERT_TRUE(blocks.has_value()) << blocks.error();
        const std::array<Triangle, 2> triangles = BentTriangles(wall, diagonal);
        const auto diagonal_edges = DiagonalEdges(diagonal);
        std::vector<std::vector<CellBox>> sealed;
        for (const Block& block : *blocks) {
          sealed.push_back(SealedBoxes(block, ShippedPalette()));
        }
        const auto is_sealed = [&sealed](const Eigen::Vector3i& cell) {
          return std::ranges::any_of(sealed, [&cell](const std::vector<CellBox>& boxes) {
            return std::ranges::any_of(boxes, [&cell](const CellBox& box) { return box.Contains(cell); });
          });
        };
        const std::array<Eigen::Vector3i, 4> corners {
            wall.node_a, wall.node_a + Eigen::Vector3i::Unit(static_cast<int>(wall.stick_a.axis)) * wall.stick_a.length,
            wall.node_b + Eigen::Vector3i::Unit(static_cast<int>(wall.stick_b.axis)) * wall.stick_b.length,
            wall.node_b};
        Eigen::Vector3i low = corners[0];
        Eigen::Vector3i high = corners[0];
        for (const Eigen::Vector3i& corner : corners) {
          low = low.cwiseMin(corner);
          high = high.cwiseMax(corner);
        }
        for (int x = low.x() - 1; x <= high.x(); ++x) {
          for (int y = low.y() - 1; y <= high.y(); ++y) {
            for (int z = low.z() - 1; z <= high.z(); ++z) {
              const Eigen::Vector3i cell(x, y, z);
              for (int axis = 0; axis < 3; ++axis) {
                const Eigen::Vector3i next = cell + Eigen::Vector3i::Unit(axis);
                if (is_sealed(cell) || is_sealed(next)) {
                  continue;
                }
                const Eigen::Vector3d centre = cell.cast<double>() + Eigen::Vector3d::Constant(0.5);
                const Eigen::Vector3d beyond = next.cast<double>() + Eigen::Vector3d::Constant(0.5);
                for (size_t i = 0; i < triangles.size(); ++i) {
                  const auto crossing = Crossing(centre, beyond, triangles[i], diagonal_edges[i]);
                  if (!crossing) {
                    continue;
                  }
                  double nearest = std::numeric_limits<double>::max();
                  for (size_t edge = 0; edge < corners.size(); ++edge) {
                    const Eigen::Vector3i& end = corners[(edge + 1) % corners.size()];
                    nearest = std::min(nearest, DistanceToSegment(*crossing, corners[edge], end));
                  }
                  EXPECT_LT(nearest, kAwayFromTheEdges) << cell.transpose() << " axis " << axis;
                }
              }
            }
          }
        }
      }
    }
  }
}

}  // namespace
}  // namespace z13::building::primitives
