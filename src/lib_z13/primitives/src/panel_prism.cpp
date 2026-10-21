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

#include <primitives/panel_prism.h>

#include <algorithm>
#include <limits>

#include <primitives/placement.h>

namespace z13::building::primitives {

namespace {

using z13::station::Block;

}  // namespace

std::expected<Block, std::string> PrismBlock(
    const PrismFrame& frame, std::span<const PrismCorner> corners, const Palette& palette) {
  if (corners.size() < kTriangleCorners || corners.size() > kSectionCorners) {
    return std::unexpected(std::string {"a panel's section has three or four corners"});
  }
  const auto lift = [&frame](const Eigen::Vector2i& plan, int along) {
    Eigen::Vector3i point;
    point[frame.x_axis] = plan.x();
    point[frame.z_axis] = plan.y();
    point[frame.y_axis] = along;
    return point;
  };
  Eigen::Vector3i lowest = Eigen::Vector3i::Constant(std::numeric_limits<int>::max());
  Eigen::Vector3i highest = Eigen::Vector3i::Constant(std::numeric_limits<int>::min());
  for (const PrismCorner& corner : corners) {
    for (const int along : {corner.from, corner.to}) {
      lowest = lowest.cwiseMin(lift(corner.plan, along));
      highest = highest.cwiseMax(lift(corner.plan, along));
    }
  }
  const Eigen::Vector3i extent = highest - lowest;
  const Eigen::Vector3i size {extent[frame.x_axis], extent[frame.y_axis], extent[frame.z_axis]};
  const auto primitive = palette.Find(z13::station::kPanelPrimitiveId);
  if (!primitive || (size.array() < 1).any() || (size.array() > primitive->get().max_size.array()).any()) {
    return std::unexpected(std::string {"the panel's size isn't allowed"});
  }

  for (const z13::station::Orientation orientation : kOrientations) {
    const Eigen::Matrix3i rotation = OrientationMatrix(orientation);
    if (rotation.col(0).cwiseAbs() != Eigen::Vector3i::Unit(frame.x_axis) ||
        rotation.col(1).cwiseAbs() != Eigen::Vector3i::Unit(frame.y_axis) ||
        rotation.col(2).cwiseAbs() != Eigen::Vector3i::Unit(frame.z_axis)) {
      continue;
    }
    Block block {.spec = {.type_id = z13::station::kPanelPrimitiveId, .size = size, .orientation = orientation},
                 .cell = lowest};
    const CellPose pose = PoseOf(block);
    const size_t section = block.spec.face_cuts.size() / 2;
    for (size_t i = 0; i < section; ++i) {
      // A triangle's fourth corner repeats its third.
      const PrismCorner& corner = corners[std::min(i, corners.size() - 1)];
      const Eigen::Vector3i near = rotation.transpose() * (lift(corner.plan, corner.from) - pose.origin);
      const Eigen::Vector3i far = rotation.transpose() * (lift(corner.plan, corner.to) - pose.origin);
      block.spec.corners[2 * i] = near.x();
      block.spec.corners[(2 * i) + 1] = near.z();
      block.spec.face_cuts[i] = std::min(near.y(), far.y());
      block.spec.face_cuts[section + i] = size.y() - std::max(near.y(), far.y());
    }
    return block;
  }
  return std::unexpected(std::string {"no turn of the panel fits the section"});
}

}  // namespace z13::building::primitives
