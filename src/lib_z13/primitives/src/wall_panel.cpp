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

#include <primitives/wall_panel.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <format>
#include <limits>
#include <set>
#include <vector>

#include <primitives/geometry.h>
#include <primitives/placement.h>

namespace z13::building::primitives {

namespace {

using z13::station::Block;
using z13::station::BlockSpec;
using z13::station::WallAxis;

using Points = std::set<std::array<int, 3>>;

std::array<int, 3> AsArray(const Eigen::Vector3i& point) {
  return {point.x(), point.y(), point.z()};
}

int Sign(int value) {
  return (value > 0) - (value < 0);
}

// The prism's eight corners in its own frame: the section (0, 0), (run, rise), (run, rise +
// thickness), (0, thickness) in XZ, at y = 0 and y = length, less `taper` at each end of X.
std::array<Eigen::Vector3i, 8> LocalCorners(const Eigen::Vector3i& size, int slant, const Eigen::Vector2i& taper) {
  const int thickness = size.z() - slant;
  const std::array<Eigen::Vector2i, 4> section {
      Eigen::Vector2i {0, 0}, {size.x(), slant}, {size.x(), size.z()}, {0, thickness}};
  std::array<Eigen::Vector3i, 8> corners;
  size_t next = 0;
  for (const bool far : {false, true}) {
    for (size_t i = 0; i < section.size(); ++i) {
      corners[next++] = {section[i].x(), PanelFaceY(size.y(), taper, i, far), section[i].y()};
    }
  }
  return corners;
}

// Turns alone reach only half of the mirror images, so the cut may be at either end and face.
std::vector<Eigen::Vector2i> TapersOf(int shortfall) {
  if (shortfall == 0) {
    return {Eigen::Vector2i::Zero()};
  }
  return {{0, shortfall}, {shortfall, 0}, {0, -shortfall}, {-shortfall, 0}};
}

}  // namespace

PlanAxes PlanAxesOf(WallAxis stick_axis, const Eigen::Vector3i& span) {
  const int stick = static_cast<int>(stick_axis);
  const int first = stick == 0 ? 1 : 0;
  const int second = stick == 2 ? 1 : 2;
  const int major = std::abs(span[first]) >= std::abs(span[second]) ? first : second;
  return {.first = first, .second = second, .major = major, .minor = major == first ? second : first};
}

std::optional<int> MinWallThickness(const WallGeometry& wall) {
  Eigen::Vector3i span = wall.node_b - wall.node_a;
  span[static_cast<int>(wall.stick_axis)] = 0;
  if (span.isZero()) {
    return std::nullopt;
  }
  const PlanAxes axes = PlanAxesOf(wall.stick_axis, span);
  const int run = std::abs(span[axes.major]);
  const int slant = std::abs(span[axes.minor]);
  return (run + slant + run - 1) / run;
}

std::expected<Block, std::string> WallPanelBlock(const WallGeometry& wall, const Palette& palette) {
  const auto primitive = palette.Find(z13::station::kPanelPrimitiveId);
  if (!primitive) {
    return std::unexpected(std::string {"the palette has no Panel"});
  }
  const int stick = static_cast<int>(wall.stick_axis);
  Eigen::Vector3i offset = wall.node_b - wall.node_a;
  offset[stick] = 0;
  if (offset.isZero()) {
    return std::unexpected(std::string {"the wall's nodes coincide in its plane"});
  }
  const int far_length = wall.stick_length + wall.stick_growth;
  if (wall.stick_length == 0 || wall.thickness == 0) {
    return std::unexpected(std::string {"the wall has no stick length or thickness"});
  }
  if (Sign(far_length) != Sign(wall.stick_length)) {
    return std::unexpected(std::string {"the wall's sticks point opposite ways or one has no length"});
  }

  const PlanAxes axes = PlanAxesOf(wall.stick_axis, offset);
  const int major = axes.major;
  const int minor = axes.minor;
  const int run = std::abs(offset[major]);
  const int slant = std::abs(offset[minor]);
  const int thickness = std::abs(wall.thickness);
  const int longest = std::max(std::abs(wall.stick_length), std::abs(far_length));
  const int shortfall = longest - std::min(std::abs(wall.stick_length), std::abs(far_length));
  const Eigen::Vector3i size {run, longest, slant + thickness};
  if (!primitive->get().shape.AllowsSize(size, slant, {0, shortfall}) ||
      (size.array() > primitive->get().max_size.array()).any()) {
    return std::unexpected(std::format(
        "a panel of run {}, rise {} and thickness {} along a stick of {} isn't allowed", run, slant, thickness,
        size.y()));
  }

  Eigen::Vector3i thicken = Eigen::Vector3i::Zero();
  thicken[minor] = Sign(wall.thickness) * thickness;
  const Eigen::Vector3i high = wall.node_a + offset;
  Points target;
  const std::array<Eigen::Vector3i, 4> bases {wall.node_a, wall.node_a + thicken, high, high + thicken};
  constexpr size_t kBasesAtA = 2;
  for (size_t i = 0; i < bases.size(); ++i) {
    Eigen::Vector3i along = Eigen::Vector3i::Zero();
    along[stick] = i < kBasesAtA ? wall.stick_length : far_length;
    target.insert(AsArray(bases[i]));
    target.insert(AsArray(bases[i] + along));
  }
  Eigen::Vector3i lowest = Eigen::Vector3i::Constant(std::numeric_limits<int>::max());
  for (const auto& point : target) {
    lowest = lowest.cwiseMin(Eigen::Vector3i(point[0], point[1], point[2]));
  }

  for (const Eigen::Vector2i& taper : TapersOf(shortfall)) {
    const auto corners = LocalCorners(size, slant, taper);
    for (const z13::station::Orientation orientation : kOrientations) {
      const Block block {.spec = {.type_id = z13::station::kPanelPrimitiveId, .size = size, .orientation = orientation,
                                  .slant = slant, .taper = taper},
                         .cell = lowest};
      const CellPose pose = PoseOf(block);
      Points placed;
      for (const Eigen::Vector3i& corner : corners) {
        placed.insert(AsArray(pose.origin + pose.rotation * corner));
      }
      if (placed == target) {
        return block;
      }
    }
  }
  return std::unexpected(std::string {"no turn of the panel fits the wall"});
}

}  // namespace z13::building::primitives
