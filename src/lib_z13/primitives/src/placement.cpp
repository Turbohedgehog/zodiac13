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

#include <z13_primitives/placement.h>

#include <algorithm>

namespace z13::building::primitives {

namespace {

using z13::station::Orientation;

enum class Axis : uint8_t { kPosX, kNegX, kPosY, kNegY, kPosZ, kNegZ };

struct Turn {
  Axis face {};
  Axis up {};
};

Turn TurnOf(Orientation orientation) {
  switch (orientation) {
    case Orientation::kFacePosXUpPosZ: return {Axis::kPosX, Axis::kPosZ};
    case Orientation::kFacePosXUpNegZ: return {Axis::kPosX, Axis::kNegZ};
    case Orientation::kFacePosXUpPosY: return {Axis::kPosX, Axis::kPosY};
    case Orientation::kFacePosXUpNegY: return {Axis::kPosX, Axis::kNegY};
    case Orientation::kFaceNegXUpPosZ: return {Axis::kNegX, Axis::kPosZ};
    case Orientation::kFaceNegXUpNegZ: return {Axis::kNegX, Axis::kNegZ};
    case Orientation::kFaceNegXUpPosY: return {Axis::kNegX, Axis::kPosY};
    case Orientation::kFaceNegXUpNegY: return {Axis::kNegX, Axis::kNegY};
    case Orientation::kFacePosYUpPosZ: return {Axis::kPosY, Axis::kPosZ};
    case Orientation::kFacePosYUpNegZ: return {Axis::kPosY, Axis::kNegZ};
    case Orientation::kFacePosYUpPosX: return {Axis::kPosY, Axis::kPosX};
    case Orientation::kFacePosYUpNegX: return {Axis::kPosY, Axis::kNegX};
    case Orientation::kFaceNegYUpPosZ: return {Axis::kNegY, Axis::kPosZ};
    case Orientation::kFaceNegYUpNegZ: return {Axis::kNegY, Axis::kNegZ};
    case Orientation::kFaceNegYUpPosX: return {Axis::kNegY, Axis::kPosX};
    case Orientation::kFaceNegYUpNegX: return {Axis::kNegY, Axis::kNegX};
    case Orientation::kFacePosZUpPosX: return {Axis::kPosZ, Axis::kPosX};
    case Orientation::kFacePosZUpNegX: return {Axis::kPosZ, Axis::kNegX};
    case Orientation::kFacePosZUpPosY: return {Axis::kPosZ, Axis::kPosY};
    case Orientation::kFacePosZUpNegY: return {Axis::kPosZ, Axis::kNegY};
    case Orientation::kFaceNegZUpPosX: return {Axis::kNegZ, Axis::kPosX};
    case Orientation::kFaceNegZUpNegX: return {Axis::kNegZ, Axis::kNegX};
    case Orientation::kFaceNegZUpPosY: return {Axis::kNegZ, Axis::kPosY};
    case Orientation::kFaceNegZUpNegY: return {Axis::kNegZ, Axis::kNegY};
  }
  return {Axis::kPosX, Axis::kPosZ};
}

Eigen::Matrix3i QuarterTurnMatrix(TurnAxis axis) {
  Eigen::Matrix3i turn;
  switch (axis) {
    case TurnAxis::kX:
      turn << 1, 0, 0, 0, 0, -1, 0, 1, 0;
      return turn;
    case TurnAxis::kY:
      turn << 0, 0, 1, 0, 1, 0, -1, 0, 0;
      return turn;
    case TurnAxis::kZ:
      turn << 0, -1, 0, 1, 0, 0, 0, 0, 1;
      return turn;
  }
  return Eigen::Matrix3i::Identity();
}

Orientation OrientationOf(const Eigen::Matrix3i& matrix) {
  const auto found = std::ranges::find_if(
      kOrientations, [&matrix](Orientation candidate) { return OrientationMatrix(candidate) == matrix; });
  return found != kOrientations.end() ? *found : Orientation {};
}

Eigen::Vector3i Direction(Axis axis) {
  switch (axis) {
    case Axis::kPosX: return Eigen::Vector3i::UnitX();
    case Axis::kNegX: return -Eigen::Vector3i::UnitX();
    case Axis::kPosY: return Eigen::Vector3i::UnitY();
    case Axis::kNegY: return -Eigen::Vector3i::UnitY();
    case Axis::kPosZ: return Eigen::Vector3i::UnitZ();
    case Axis::kNegZ: return -Eigen::Vector3i::UnitZ();
  }
  return Eigen::Vector3i::Zero();
}

}  // namespace

// +Y follows from +X and +Z, as in any right-handed frame.
Eigen::Matrix3i OrientationMatrix(Orientation orientation) {
  const Turn turn = TurnOf(orientation);
  const Eigen::Vector3i face = Direction(turn.face);
  const Eigen::Vector3i up = Direction(turn.up);
  Eigen::Matrix3i matrix;
  matrix << face, up.cross(face), up;
  return matrix;
}

Orientation InverseOrientation(Orientation orientation) {
  return OrientationOf(OrientationMatrix(orientation).transpose());
}

Orientation QuarterTurn(Orientation orientation, TurnAxis axis) {
  return OrientationOf(QuarterTurnMatrix(axis) * OrientationMatrix(orientation));
}

bool CellBox::Contains(const Eigen::Vector3i& cell) const {
  return (cell.array() >= min.array()).all() && (cell.array() < End().array()).all();
}

bool CellBox::Overlaps(const CellBox& other) const {
  return (min.array() < other.End().array()).all() && (other.min.array() < End().array()).all();
}

CellBox OccupiedCells(const z13::station::Block& block) {
  return {.min = block.cell, .extent = OrientationMatrix(block.spec.orientation).cwiseAbs() * block.spec.size};
}

// The rotated frame spans [rotation * 0, rotation * size] per axis in some order; shifting
// its lowest corner onto `cell` gives the origin.
CellPose PoseOf(const z13::station::Block& block) {
  const Eigen::Matrix3i rotation = OrientationMatrix(block.spec.orientation);
  const Eigen::Vector3i rotated_size = rotation * block.spec.size;
  return {.rotation = rotation, .origin = block.cell - rotated_size.cwiseMin(Eigen::Vector3i::Zero())};
}

z13::station::Block PlaceCentredOn(const Eigen::Vector3f& point, const z13::station::BlockSpec& spec) {
  const Eigen::Vector3i extent = OrientationMatrix(spec.orientation).cwiseAbs() * spec.size;
  const Eigen::Vector3f lowest = point - extent.cast<float>() / 2.f;
  return {.spec = spec, .cell = lowest.array().round().cast<int>()};
}

z13::station::Block DraggedBlock(
    const Eigen::Vector3i& from_cell, const Eigen::Vector3i& to_cell, const z13::station::BlockSpec& brush,
    const Eigen::Vector3i& min_size, const Eigen::Vector3i& max_size) {
  // A permutation: world extent = axes * own size, own size = axes^T * world extent.
  const Eigen::Matrix3i axes = OrientationMatrix(brush.orientation).cwiseAbs();
  const Eigen::Vector3i delta = to_cell - from_cell;
  const Eigen::Vector3i wanted = (delta.array() != 0).select(delta.cwiseAbs(), axes * brush.size);
  const Eigen::Vector3i size = (axes.transpose() * wanted).cwiseMax(min_size).cwiseMin(max_size);
  const Eigen::Vector3i extent = axes * size;

  const z13::station::BlockSpec spec {.type_id = brush.type_id, .size = size, .orientation = brush.orientation};
  const Eigen::Vector3f from_centre = from_cell.cast<float>() + Eigen::Vector3f::Constant(0.5f);
  const Eigen::Vector3i centred = PlaceCentredOn(from_centre, spec).cell;
  const Eigen::Vector3i backwards = from_cell - extent + Eigen::Vector3i::Ones();
  const Eigen::Vector3i cell =
      (delta.array() > 0).select(from_cell, (delta.array() < 0).select(backwards, centred));
  return {.spec = spec, .cell = cell};
}

Eigen::Isometry3f WorldPose(const CellPose& pose, float cell_size) {
  Eigen::Isometry3f world = Eigen::Isometry3f::Identity();
  world.linear() = pose.rotation.cast<float>();
  world.translation() = pose.origin.cast<float>() * cell_size;
  return world;
}

}  // namespace z13::building::primitives
