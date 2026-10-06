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

#include <cstdint>

#include <Eigen/Dense>

// Placing a primitive on the grid: which cells it occupies and where its own frame (see
// geometry.h) lands, in cells.
namespace z13::primitives {

// The rotations that map the grid onto itself; 0 is the identity.
inline constexpr uint8_t kOrientationCount = 24;

constexpr bool IsValidOrientation(uint8_t orientation) {
  return orientation < kOrientationCount;
}

// `orientation` must be valid.
Eigen::Matrix3i OrientationMatrix(uint8_t orientation);
uint8_t InverseOrientation(uint8_t orientation);

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

// A primitive of `size` turned by `orientation`, with `cell` the lowest occupied corner.
struct Placement {
  Eigen::Vector3i cell = Eigen::Vector3i::Zero();
  Eigen::Vector3i size = Eigen::Vector3i::Ones();
  uint8_t orientation {};

  CellBox Occupied() const;
  CellPose Pose() const;
};

// The placement of `size`/`orientation` whose occupied cells are centred on `point`
// (in cells), rounded to the grid.
Placement PlaceCentredOn(const Eigen::Vector3f& point, const Eigen::Vector3i& size, uint8_t orientation);

// The pose in meters: rotation, then the origin scaled by `cell_size`. Geometry built in
// cells must be scaled by `cell_size` before it.
Eigen::Isometry3f WorldPose(const CellPose& pose, float cell_size);

}  // namespace z13::primitives
