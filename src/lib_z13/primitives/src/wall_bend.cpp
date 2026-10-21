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

#include <primitives/wall_bend.h>

#include <algorithm>
#include <utility>

namespace z13::building::primitives {

namespace {

using z13::station::Block;
using z13::station::WallDiagonal;
using z13::station::WallStick;

Eigen::Vector3i Along(const WallStick& stick) {
  Eigen::Vector3i along = Eigen::Vector3i::Zero();
  along[static_cast<int>(stick.axis)] = stick.length;
  return along;
}

int Sign(int64_t value) {
  return (value > 0) - (value < 0);
}

// The cosine of the angle between the triangles' normals, as its sign and its square.
struct Fold {
  int sign {};
  double cosine_squared {};
};

Fold FoldOf(const std::array<Triangle, 2>& triangles) {
  const auto first = TriangleNormal(triangles[0]);
  const auto second = TriangleNormal(triangles[1]);
  const int64_t dot = first.dot(second);
  const double lengths = static_cast<double>(first.squaredNorm()) * static_cast<double>(second.squaredNorm());
  const double product = static_cast<double>(dot) * static_cast<double>(dot);
  return {.sign = Sign(dot), .cosine_squared = lengths == 0.0 ? 0.0 : product / lengths};
}

// Whether `a` folds the surface less: its normals' cosine is the larger, by more than rounding.
bool Flatter(const Fold& a, const Fold& b) {
  constexpr double kTie = 1e-12;
  if (a.sign != b.sign) {
    return a.sign > b.sign;
  }
  const double gain = a.cosine_squared - b.cosine_squared;
  return a.sign >= 0 ? gain > kTie : gain < -kTie;
}

}  // namespace

bool IsBent(const WallGeometry& wall) {
  return wall.stick_a.axis != wall.stick_b.axis;
}

std::array<Triangle, 2> BentTriangles(const WallGeometry& wall, WallDiagonal diagonal) {
  const Eigen::Vector3i a_base = wall.node_a;
  const Eigen::Vector3i a_tip = wall.node_a + Along(wall.stick_a);
  const Eigen::Vector3i b_base = wall.node_b;
  const Eigen::Vector3i b_tip = wall.node_b + Along(wall.stick_b);
  if (diagonal == WallDiagonal::kBaseToTip) {
    return {Triangle {a_base, a_tip, b_tip}, Triangle {a_base, b_tip, b_base}};
  }
  return {Triangle {a_base, a_tip, b_base}, Triangle {a_tip, b_tip, b_base}};
}

WallDiagonal DefaultDiagonal(const WallGeometry& wall) {
  const Fold tip_to_base = FoldOf(BentTriangles(wall, WallDiagonal::kTipToBase));
  const Fold base_to_tip = FoldOf(BentTriangles(wall, WallDiagonal::kBaseToTip));
  return Flatter(tip_to_base, base_to_tip) ? WallDiagonal::kTipToBase : WallDiagonal::kBaseToTip;
}

std::optional<int> MinBentThickness(const WallGeometry& wall) {
  const std::array<Triangle, 2> triangles = BentTriangles(wall, wall.diagonal);
  const auto first = MinTriangleThickness(triangles[0]);
  const auto second = MinTriangleThickness(triangles[1]);
  if (!first || !second) {
    return std::nullopt;
  }
  return std::max(*first, *second);
}

std::expected<std::vector<Block>, std::string> WallPanelBlocks(const WallGeometry& wall, const Palette& palette) {
  if (!IsBent(wall)) {
    auto block = WallPanelBlock(wall, palette);
    if (!block) {
      return std::unexpected(std::move(block.error()));
    }
    return std::vector<Block> {*block};
  }
  if (wall.stick_a.length == 0 || wall.stick_b.length == 0 || wall.thickness == 0) {
    return std::unexpected(std::string {"the wall has no stick length or thickness"});
  }
  std::vector<Block> blocks;
  for (const Triangle& triangle : BentTriangles(wall, wall.diagonal)) {
    const auto axis = ThickeningAxisOf(triangle);
    if (!axis) {
      return std::unexpected(std::string {"a triangle of the wall has its corners on one line"});
    }
    const int toward_normal = TriangleNormal(triangle)[*axis] > 0 ? 1 : -1;
    auto block = TrianglePanelBlock(triangle, toward_normal * wall.thickness, palette);
    if (!block) {
      return std::unexpected(std::move(block.error()));
    }
    blocks.push_back(*block);
  }
  return blocks;
}

}  // namespace z13::building::primitives
