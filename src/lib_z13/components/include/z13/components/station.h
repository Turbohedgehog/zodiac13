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

// The 24 turns that map the grid onto itself, named by where the primitive's +X faces and
// where its +Z points. The identity comes first.
enum class Orientation : uint8_t {
  kFacePosXUpPosZ,
  kFacePosXUpNegZ,
  kFacePosXUpPosY,
  kFacePosXUpNegY,
  kFaceNegXUpPosZ,
  kFaceNegXUpNegZ,
  kFaceNegXUpPosY,
  kFaceNegXUpNegY,
  kFacePosYUpPosZ,
  kFacePosYUpNegZ,
  kFacePosYUpPosX,
  kFacePosYUpNegX,
  kFaceNegYUpPosZ,
  kFaceNegYUpNegZ,
  kFaceNegYUpPosX,
  kFaceNegYUpNegX,
  kFacePosZUpPosX,
  kFacePosZUpNegX,
  kFacePosZUpPosY,
  kFacePosZUpNegY,
  kFaceNegZUpPosX,
  kFaceNegZUpNegX,
  kFaceNegZUpPosY,
  kFaceNegZUpNegY,
};

// `size` is along the primitive's own axes, before `orientation` turns it.
struct BlockSpec {
  uint32_t type_id {};
  Eigen::Vector3i size = Eigen::Vector3i::Ones();
  Orientation orientation {};
};

inline BlockSpec CubeSpec() {
  return {.type_id = kCubePrimitiveId, .size = Eigen::Vector3i::Constant(kCubeEdgeCells)};
}

// A primitive placed on the grid; `cell` is the lowest corner of the cells it occupies.
struct Block {
  using State = void;
  BlockSpec spec;
  Eigen::Vector3i cell = Eigen::Vector3i::Zero();
};

// What a player's next build places, at the brush.
struct BlockBrush {
  using State = void;
  BlockSpec spec;
};

// A spot SpawnPlayer puts players on, with their facing.
struct SpawnPoint {
  using State = void;
  Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
};

}  // namespace z13::station
