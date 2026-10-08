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

#include <z13_rooms/portal_source.h>

#include <Eigen/Dense>

namespace z13::station::rooms {

namespace {

using z13::building::primitives::CellBox;
using z13::building::primitives::CellPose;
using z13::building::primitives::Primitive;
using z13::building::primitives::PrimitiveFlags;
using z13::building::primitives::ShapeKind;

// The thin axis of every door and window is the primitive's own Y.
constexpr int kThicknessAxis = 1;

CellBox InPrimitiveFrame(const Block& block, const Primitive& primitive, bool door_gap) {
  const Eigen::Vector3i size = block.spec.size;
  if (!door_gap) {
    return {.min = Eigen::Vector3i::Zero(), .extent = size};
  }
  const Eigen::Vector2i gap = primitive.shape.door_opening;
  return {.min = {(size.x() - gap.x()) / 2, 0, 0}, .extent = {gap.x(), size.y(), gap.y()}};
}

CellBox InWorld(const CellBox& box, const CellPose& pose) {
  const Eigen::Vector3i first = pose.origin + pose.rotation * box.min;
  const Eigen::Vector3i second = pose.origin + pose.rotation * box.End();
  return {.min = first.cwiseMin(second), .extent = (second - first).cwiseAbs()};
}

}  // namespace

bool IsSealing(const Primitive& primitive) {
  return primitive.Has(PrimitiveFlags::GasSealing);
}

std::optional<PortalSource> PortalOf(const Block& block, const Primitive& primitive) {
  const bool door = primitive.shape.kind == ShapeKind::kDoorFrame;
  const bool transparent = primitive.Has(PrimitiveFlags::Transparent);
  if (!door && !transparent) {
    return std::nullopt;
  }
  const CellPose pose = z13::building::primitives::PoseOf(block);
  Eigen::Index axis {};
  pose.rotation.col(kThicknessAxis).cwiseAbs().maxCoeff(&axis);
  return PortalSource {
      .opening = InWorld(InPrimitiveFrame(block, primitive, door), pose),
      .axis = static_cast<Axis>(axis),
      .visible = transparent,
      // Doors stay closed until they have a state (f/doors).
      .passable = false,
  };
}

}  // namespace z13::station::rooms
