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

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

#include <Eigen/Dense>

#include <z13/components/block.h>
#include <z13/components/player_action.h>

namespace z13::station {

// What a player's next build places, at the brush.
struct BlockBrush {
  using State = void;
  BlockSpec spec;
};

// A player dragging out a block: the brush's cell where the drag began, and the normal of
// the face it was aimed at (zero in free space).
struct BrushDrag {
  using State = void;
  Eigen::Vector3i anchor_cell = Eigen::Vector3i::Zero();
  Eigen::Vector3i anchor_normal = Eigen::Vector3i::Zero();
};

// The right to build and destroy blocks; every station player gets it for now.
struct BuildPermission {
  using State = void;
};

// What a brush's build would place right now and whether it would be accepted; derived
// every frame on the brush entity, never state.
// A line the brush draws over its block, in cells.
struct BrushLine {
  Eigen::Vector3f from = Eigen::Vector3f::Zero();
  Eigen::Vector3f to = Eigen::Vector3f::Zero();
};

struct BrushPreview {
  enum class Kind : uint8_t { kBuild, kCutIn, kCut };

  Block block;
  bool valid {};
  // A cut's block is a plain box of the cells it would take.
  Kind kind {};
  std::vector<BrushLine> lines;
};

// The palette window's pick goes out as an action value, slot + 1, which a logged action
// value can hold only up to this many slots.
inline constexpr size_t kPaletteWindowSlots =
    static_cast<size_t>(std::numeric_limits<int16_t>::max() / z13::gameplay::kActionValueScale);

// The local player's pick in the palette window (an index into the palette), until it is
// sent as an action.
struct PaletteChoice {
  using Singleton = void;
  std::optional<size_t> slot;
};

}  // namespace z13::station
