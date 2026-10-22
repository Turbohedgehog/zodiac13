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
#include <cstdint>

#include <Eigen/Dense>

namespace z13::station {

// Edge of a grid cell in meters: the only place cells become world positions.
inline constexpr float kCellSize = 0.25f;

// The ordinary scene's building cube (assets/station/palette.json).
inline constexpr uint32_t kCubePrimitiveId = 9;
inline constexpr int kCubeEdgeCells = 2;

// The wall panel (assets/station/palette.json): a parallelogram prism of any direction.
inline constexpr uint32_t kPanelPrimitiveId = 12;

// The pane of a wall's window (assets/station/palette.json): a transparent Panel.
inline constexpr uint32_t kGlassPanelPrimitiveId = 30;

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

// A panel section's four corners as x and z of each, in the primitive's own frame: the reference
// face's near and far end, then the thickened face's far and near end (the order of the
// square-cut section). All zero where nothing cut them.
using PanelCorners = std::array<int, 8>;

// How much each of a panel section's four corners (in PanelCorners' order) is cut off the length
// along Y: the first four off the near face, the last four off the far one. All zero where the
// faces are flat.
using PanelFaceCuts = std::array<int, 8>;

// `size` is along the primitive's own axes, before `orientation` turns it.
struct BlockSpec {
  uint32_t type_id {};
  Eigen::Vector3i size = Eigen::Vector3i::Ones();
  Orientation orientation {};
  // A panel's rise over its size's X (primitives.fbs, Panel); zero for every other primitive.
  int slant {};
  // Where the ends of a panel that a joint cuts stand; zero for a square-cut one.
  PanelCorners corners {};
  // Where the faces of a panel with sloped ends (a trapezoid) stand along Y; zero for flat ones.
  PanelFaceCuts face_cuts {};

  bool HasCorners() const { return corners != PanelCorners {}; }
  bool HasFaceCuts() const { return face_cuts != PanelFaceCuts {}; }
  bool operator==(const BlockSpec&) const = default;
};

inline BlockSpec CubeSpec() {
  return {.type_id = kCubePrimitiveId, .size = Eigen::Vector3i::Constant(kCubeEdgeCells)};
}

// A primitive placed on the grid; `cell` is the lowest corner of the cells it occupies.
struct Block {
  using State = void;
  BlockSpec spec;
  Eigen::Vector3i cell = Eigen::Vector3i::Zero();

  bool operator==(const Block&) const = default;
};

}  // namespace z13::station
