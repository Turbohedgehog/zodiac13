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

// Walls of any direction (docs/oblique-walls-plan.md, stage 5): nodes on the grid, segments
// between them, and the panel block a system derives from each segment.
namespace z13::station {

// The axis a segment's "stick" runs along: a wall's height, or a floor's or ramp's width.
enum class WallAxis : uint8_t { kX, kY, kZ };

// A point on the grid; nodes are shared by id, which every participant has, unlike entity ids.
struct WallNode {
  using State = void;
  uint32_t id {};
  Eigen::Vector3i cell = Eigen::Vector3i::Zero();
};

// The panel between two sticks: one at node `node_a`, the other at `node_b` with its offset
// along the stick dropped. `stick_length` is signed (the stick's direction along `stick_axis`);
// the sign of `thickness` picks the side of the line from a to b that the panel thickens to.
struct WallSegment {
  using State = void;
  uint32_t id {};
  uint32_t node_a {};
  uint32_t node_b {};
  WallAxis stick_axis = WallAxis::kZ;
  int stick_length {};
  int thickness {};
};

// A player's wall in the making (the Wall brush mode): the first click set `first`; the second
// the other end of the stick, in a plane through `first`, with `stick_axis` left over as the
// axis the third click measures the wall's other edge along.
struct WallDraft {
  using State = void;
  enum class Stage : uint8_t { kStick, kHeight };
  Stage stage {};
  Eigen::Vector3i first = Eigen::Vector3i::Zero();
  Eigen::Vector3i second = Eigen::Vector3i::Zero();
  WallAxis stick_axis = WallAxis::kZ;
};

// A player's first click of the Merge mode: the node the second click merges into.
struct WallMergeDraft {
  using State = void;
  uint32_t node_id {};
};

// Marks the block derived from a segment; the segment owns it (WallPanelSystem).
struct WallPanelOf {
  using State = void;
  uint32_t segment_id {};
};

}  // namespace z13::station
