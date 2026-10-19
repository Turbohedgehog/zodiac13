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

#include <expected>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <Eigen/Dense>
#include <flecs.h>

#include <z13/components/block.h>
#include <z13/components/block_brush.h>
#include <z13/components/wall.h>
#include <primitives/palette.h>
#include <primitives/wall_panel.h>

#include "brush_aim.h"

namespace z13::building {

// How a click finds its point: anchors within `snap_cells` win over the grid, and the view ray
// looks for planes up to `reach_cells` away.
struct WallPicking {
  float snap_cells {};
  float reach_cells {};
};

// The grid point a click at `aim` sets: the nearest of `anchors` within `snap_cells`, else
// the aimed point rounded to the grid.
Eigen::Vector3i WallClickPoint(
    const Aim& aim, std::span<const Eigen::Vector3i> anchors, float snap_cells);

// Where clicks snap to: every wall node and the far end of every segment's stick.
std::vector<Eigen::Vector3i> WallAnchors(flecs::world world);

// The stick's far end: where the view ray meets the plane (xy, yz or zx) through `first` that
// the aimed face lies in, else the nearest of the three it crosses, snapped to an anchor in that
// plane; `axis` is the plane's own axis, the one left over. Nothing when no plane is met or the
// stick would have no length.
struct StickPick {
  Eigen::Vector3i point = Eigen::Vector3i::Zero();
  z13::station::WallAxis axis = z13::station::WallAxis::kZ;
};

std::optional<StickPick> PickStick(
    const Aim& aim, const Eigen::Vector3i& first, std::span<const Eigen::Vector3i> anchors,
    const WallPicking& picking);

// The signed length along `axis` from `first` where the view ray meets the plane of the stick
// (first to second) and the axis, snapped to an anchor on the axis line. A ray along that
// plane takes the point where it comes nearest the line. Nothing for a ray along the axis, one
// that points away, or a length of zero.
std::optional<int> PickHeight(
    const Aim& aim, const Eigen::Vector3i& first, const Eigen::Vector3i& second, z13::station::WallAxis axis,
    std::span<const Eigen::Vector3i> anchors, const WallPicking& picking);

// What a click makes of the draft: the next draft, or with the third click the wall (its
// thickness unsigned until FacingEye).
struct WallClick {
  std::optional<z13::station::WallDraft> draft;
  std::optional<z13::building::primitives::WallGeometry> wall;
};

std::expected<WallClick, std::string> ClickWall(
    const std::optional<z13::station::WallDraft>& draft, const Aim& aim,
    std::span<const Eigen::Vector3i> anchors, const WallPicking& picking, int thickness);

// A wall and the panel block it makes.
struct FacedWall {
  z13::building::primitives::WallGeometry wall;
  z13::station::Block block;
};

// The wall thickened to whichever side is nearer `eye`.
std::expected<FacedWall, std::string> FacingEye(
    z13::building::primitives::WallGeometry wall, const Eigen::Vector3f& eye,
    const z13::building::primitives::Palette& palette);

// A one-cell cube at `cell`, what the brush shows for a point.
z13::station::Block CellMarker(const Eigen::Vector3i& cell);

// What the brush shows for a click: the point, then the stick with an arrow along the axis
// the third click measures, then the panel. `makeable` is false when the click would make
// nothing (no plane met, no stick, a panel the palette doesn't allow).
struct WallPreview {
  z13::station::Block block;
  std::vector<z13::station::BrushLine> lines;
  bool makeable {};
};

WallPreview PreviewWall(
    const std::optional<z13::station::WallDraft>& draft, const Aim& aim,
    std::span<const Eigen::Vector3i> anchors, const WallPicking& picking, int thickness,
    const z13::building::primitives::Palette& palette);

}  // namespace z13::building
