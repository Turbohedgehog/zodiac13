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

#include <primitives/triangle_panel.h>

#include <cstdlib>
#include <format>

#include <primitives/panel_prism.h>

namespace z13::building::primitives {

namespace {

using Normal = Eigen::Vector3<int64_t>;

Normal NormalOf(const Triangle& triangle) {
  const Normal first = (triangle[1] - triangle[0]).cast<int64_t>();
  const Normal second = (triangle[2] - triangle[0]).cast<int64_t>();
  return first.cross(second);
}

}  // namespace

std::optional<int> ThickeningAxisOf(const Triangle& triangle) {
  const Normal normal = NormalOf(triangle);
  if (normal.isZero()) {
    return std::nullopt;
  }
  Eigen::Index axis = 0;
  normal.cwiseAbs().maxCoeff(&axis);
  return static_cast<int>(axis);
}

std::optional<int> MinTriangleThickness(const Triangle& triangle) {
  const auto axis = ThickeningAxisOf(triangle);
  if (!axis) {
    return std::nullopt;
  }
  const Normal normal = NormalOf(triangle).cwiseAbs();
  const int64_t along = normal[*axis];
  return static_cast<int>((normal.sum() + along - 1) / along);
}

std::expected<z13::station::Block, std::string> TrianglePanelBlock(
    const Triangle& triangle, int thickness, const Palette& palette) {
  const auto axis = ThickeningAxisOf(triangle);
  if (!axis) {
    return std::unexpected(std::string {"the triangle's corners lie on one line"});
  }
  const int least = *MinTriangleThickness(triangle);
  if (thickness == 0 || std::abs(thickness) < least) {
    return std::unexpected(std::format("a wall on this triangle needs a thickness of at least {}", least));
  }
  const int x_axis = *axis == 0 ? 1 : 0;
  const int z_axis = *axis == 2 ? 1 : 2;
  std::array<PrismCorner, 3> corners;
  for (size_t i = 0; i < triangle.size(); ++i) {
    const int level = triangle[i][*axis];
    corners[i] = {.plan = {triangle[i][x_axis], triangle[i][z_axis]},
                  .from = level,
                  .to = level + thickness};
  }
  return PrismBlock({.x_axis = x_axis, .y_axis = *axis, .z_axis = z_axis}, corners, palette);
}

}  // namespace z13::building::primitives
