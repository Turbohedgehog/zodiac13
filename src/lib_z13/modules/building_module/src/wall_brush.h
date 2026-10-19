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
#include <z13/components/wall.h>
#include <primitives/palette.h>
#include <primitives/wall_panel.h>

#include "brush_aim.h"

namespace z13::building {

// The grid point a click at `aim` sets: the nearest of `anchors` within `snap_cells`, else
// the aimed point rounded to the grid.
Eigen::Vector3i WallClickPoint(
    const Aim& aim, std::span<const Eigen::Vector3i> anchors, float snap_cells);

// Where clicks snap to: every wall node and the far end of every segment's stick.
std::vector<Eigen::Vector3i> WallAnchors(flecs::world world);

// What a click at `point` makes of the draft: the next draft, or with the third click the
// wall (its thickness unsigned until FacingEye).
struct WallClick {
  std::optional<z13::station::WallDraft> draft;
  std::optional<z13::building::primitives::WallGeometry> wall;
};

std::expected<WallClick, std::string> ClickWall(
    const std::optional<z13::station::WallDraft>& draft, const Eigen::Vector3i& point, int thickness);

// A wall and the panel block it makes.
struct FacedWall {
  z13::building::primitives::WallGeometry wall;
  z13::station::Block block;
};

// The wall thickened to whichever side is nearer `eye`.
std::expected<FacedWall, std::string> FacingEye(
    z13::building::primitives::WallGeometry wall, const Eigen::Vector3f& eye,
    const z13::building::primitives::Palette& palette);

// What the brush shows for a click at `point`: the point, the stick, or the panel; `makeable`
// is false when the click would make nothing (no stick, a panel the palette doesn't allow).
struct WallPreview {
  z13::station::Block block;
  bool makeable {};
};

WallPreview PreviewWall(
    const std::optional<z13::station::WallDraft>& draft, const Eigen::Vector3i& point, int thickness,
    const Eigen::Vector3f& eye, const z13::building::primitives::Palette& palette);

}  // namespace z13::building
