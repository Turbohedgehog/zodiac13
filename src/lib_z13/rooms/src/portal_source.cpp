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

#include <rooms/portal_source.h>

#include <Eigen/Dense>

#include <primitives/occupancy.h>

namespace z13::station::rooms {

namespace {

using z13::building::primitives::CellBox;
using z13::building::primitives::CellPose;
using z13::building::primitives::Palette;
using z13::building::primitives::Primitive;
using z13::building::primitives::PrimitiveFlags;
using z13::building::primitives::ShapeKind;

// The thin axis of every door and window is the primitive's own Y, of a panel its Z.
constexpr int kThicknessAxis = 1;
constexpr int kPanelThicknessAxis = 2;

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

std::vector<PortalSource> PortalsOf(const Block& block, const Primitive& primitive, const Palette& palette) {
  const bool door_frame = primitive.shape.kind == ShapeKind::kDoorFrame;
  const bool transparent = primitive.Has(PrimitiveFlags::Transparent);
  if (!door_frame && !transparent && !primitive.Has(PrimitiveFlags::Door)) {
    return {};
  }
  const bool panel = primitive.shape.kind == ShapeKind::kPanel;
  const CellPose pose = z13::building::primitives::PoseOf(block);
  Eigen::Index axis {};
  pose.rotation.col(panel ? kPanelThicknessAxis : kThicknessAxis).cwiseAbs().maxCoeff(&axis);
  // Doors stay closed until they have a state (f/doors).
  const PortalSource portal {.axis = static_cast<Axis>(axis), .visible = transparent, .passable = false};
  const bool square = !panel || (block.spec.slant == 0 && !block.spec.HasCorners() && !block.spec.HasFaceCuts());
  if (square) {
    PortalSource whole = portal;
    whole.opening = InWorld(InPrimitiveFrame(block, primitive, door_frame), pose);
    return {whole};
  }
  std::vector<PortalSource> portals;
  for (const CellBox& box : z13::building::primitives::SealedBoxes(block, palette)) {
    PortalSource step = portal;
    step.opening = box;
    portals.push_back(step);
  }
  return portals;
}

}  // namespace z13::station::rooms
