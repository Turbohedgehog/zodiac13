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

#include <numbers>
#include <unordered_map>

#include <Eigen/Geometry>
#include <flecs.h>

#include <z13/components/station/block_spec.h>
#include <z13/components/station/grid.h>

namespace z13::station {

// Where a grid's own meters stand in the world's: the origin (in cells) moves them, the yaw turns them about Z, then
// the pitch tilts them about the grid's Y, so a positive pitch raises the +X end.
inline Eigen::Isometry3f GridToWorld(const Grid& grid) {
  constexpr float kRadiansPerDegree = std::numbers::pi_v<float> / 180.f;
  Eigen::Isometry3f pose = Eigen::Isometry3f::Identity();
  pose.translation() = grid.origin.cast<float>() * kCellSize;
  pose.linear() = (Eigen::AngleAxisf(grid.yaw_degrees * kRadiansPerDegree, Eigen::Vector3f::UnitZ()) *
                   Eigen::AngleAxisf(-grid.pitch_degrees * kRadiansPerDegree, Eigen::Vector3f::UnitY()))
                      .toRotationMatrix();
  return pose;
}

// The pose of every grid by its id.
using GridPoses = std::unordered_map<GridId, Eigen::Isometry3f>;

inline GridPoses PosesOf(const flecs::query<const Grid>& grids) {
  GridPoses poses;
  grids.each([&poses](const Grid& grid) { poses.emplace(grid.id, GridToWorld(grid)); });
  return poses;
}

// A block's pose among the world's: its grid's pose, or the grid's own if it is unknown (as if the pose were none).
inline Eigen::Isometry3f PoseOfGrid(const GridPoses& poses, GridId grid) {
  const auto found = poses.find(grid);
  return found != poses.end() ? found->second : Eigen::Isometry3f::Identity();
}

}  // namespace z13::station
