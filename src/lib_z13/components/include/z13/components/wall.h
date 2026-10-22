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

// A segment's edge at one of its nodes: from the node along `axis` by `length`, signed.
struct WallStick {
  WallAxis axis = WallAxis::kZ;
  int length {};

  bool operator==(const WallStick&) const = default;
};

// Of the four corners of a segment's sticks (a's base and tip, b's base and tip), which two a
// bent wall's two triangles share: a's base with b's tip, or a's tip with b's base.
enum class WallDiagonal : uint8_t { kBaseToTip, kTipToBase };

// The panel between two sticks, one at each node: one panel for sticks of one axis (b's offset
// along it dropped, so both start level; unequal lengths slope the top), else two triangles
// along `diagonal`. The sign of `thickness` picks the side: of the line from a to b for one
// panel, of the surface (normal as a base, a tip, b tip, b base wind) for triangles.
struct WallSegment {
  using State = void;
  uint32_t id {};
  uint32_t node_a {};
  uint32_t node_b {};
  WallStick stick_a;
  WallStick stick_b;
  int thickness {};
  WallDiagonal diagonal {};
};

// A player's wall in the making (the Wall brush mode): the first click set `first`; the second
// the other end of the stick, in a plane through `first`, with `stick_axis` left over as the
// axis the third click measures the wall's other edge along. A bent draft takes a fourth click
// for the stick at `second` (`stick_length` is the first's), and `flipped` the other diagonal.
struct WallDraft {
  using State = void;
  enum class Stage : uint8_t { kStick, kHeight, kTip };
  Stage stage {};
  Eigen::Vector3i first = Eigen::Vector3i::Zero();
  Eigen::Vector3i second = Eigen::Vector3i::Zero();
  WallAxis stick_axis = WallAxis::kZ;
  int stick_length {};
  bool bent {};
  bool flipped {};
};

// A player's first click of the Merge mode: the node the second click merges into.
struct WallMergeDraft {
  using State = void;
  uint32_t node_id {};
};

// A player's first click of the Bridge mode: the node whose stick the second click bridges to,
// and whether a bent wall takes the diagonal other than the one that folds it least.
struct WallBridgeDraft {
  using State = void;
  uint32_t node_id {};
  bool flipped {};
};

// Marks a block derived from a segment (`part` of its one or two); the segment owns it
// (WallPanelSystem).
struct WallPanelOf {
  using State = void;
  uint32_t segment_id {};
  uint32_t part {};
};

}  // namespace z13::station
