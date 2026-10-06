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

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <Eigen/Dense>

#include <z13/components/station.h>

// Placing a block on the grid: which cells it occupies and where its primitive's own frame
// (see geometry.h) lands, in cells.
namespace z13::building::primitives {

inline constexpr size_t kOrientationCount = 24;
static_assert(static_cast<size_t>(z13::station::Orientation::kFaceNegZUpNegY) + 1 == kOrientationCount);

consteval std::array<z13::station::Orientation, kOrientationCount> AllOrientations() {
  std::array<z13::station::Orientation, kOrientationCount> all {};
  for (size_t i = 0; i < kOrientationCount; ++i) {
    all[i] = static_cast<z13::station::Orientation>(i);
  }
  return all;
}

inline constexpr std::array<z13::station::Orientation, kOrientationCount> kOrientations = AllOrientations();

// Columns: where the primitive's +X, +Y and +Z point.
Eigen::Matrix3i OrientationMatrix(z13::station::Orientation orientation);
z13::station::Orientation InverseOrientation(z13::station::Orientation orientation);

// The cells [min, min + extent).
struct CellBox {
  Eigen::Vector3i min = Eigen::Vector3i::Zero();
  Eigen::Vector3i extent = Eigen::Vector3i::Zero();

  Eigen::Vector3i End() const { return min + extent; }
  bool Contains(const Eigen::Vector3i& cell) const;
  bool Overlaps(const CellBox& other) const;
};

// Maps a point p of the primitive's frame to origin + rotation * p.
struct CellPose {
  Eigen::Matrix3i rotation = Eigen::Matrix3i::Identity();
  Eigen::Vector3i origin = Eigen::Vector3i::Zero();
};

CellBox OccupiedCells(const z13::station::Block& block);
CellPose PoseOf(const z13::station::Block& block);

// The block of `spec` whose occupied cells are centred on `point` (in cells), rounded to
// the grid.
z13::station::Block PlaceCentredOn(const Eigen::Vector3f& point, const z13::station::BlockSpec& spec);

// The pose in meters: rotation, then the origin scaled by `cell_size`. Geometry built in
// cells must be scaled by `cell_size` before it.
Eigen::Isometry3f WorldPose(const CellPose& pose, float cell_size);

}  // namespace z13::building::primitives
