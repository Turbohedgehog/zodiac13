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

namespace z13::station {

// Edge of a grid cell in meters: the only place cells become world positions.
inline constexpr float kCellSize = 0.25f;

// The ordinary scene's building cube (assets/station/palette.json).
inline constexpr uint32_t kCubePrimitiveId = 9;
inline constexpr int kCubeEdgeCells = 2;

// The world is a station-building game rather than the ship scene. State, so a joining
// client gets the server's mode with Welcome's snapshot.
struct StationMode {
  using State = void;
  using Singleton = void;
};

// A palette primitive placed on the grid. `size` is along the primitive's own axes;
// `orientation` (one of z13_primitives' 24) turns it, and `cell` is the lowest corner of
// the cells it then occupies.
struct Block {
  using State = void;
  uint32_t type_id {};
  Eigen::Vector3i cell = Eigen::Vector3i::Zero();
  Eigen::Vector3i size = Eigen::Vector3i::Ones();
  uint8_t orientation {};
};

// What a player's next build places, at the brush.
struct BlockBrush {
  using State = void;
  uint32_t type_id {};
  Eigen::Vector3i size = Eigen::Vector3i::Ones();
  uint8_t orientation {};
};

// A spot SpawnPlayer puts players on, with their facing.
struct SpawnPoint {
  using State = void;
  Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
};

}  // namespace z13::station
