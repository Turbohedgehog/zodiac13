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
#include <span>
#include <string>
#include <vector>

#include <z13/components/block.h>
#include <z13/components/wall.h>
#include <primitives/palette.h>
#include <primitives/wall_seam.h>

namespace z13::building::primitives {

// An opening leaves at least this many steps of wall at each end of the wall, and a cell above it.
inline constexpr int kMinSideSteps = 1;
inline constexpr int kMinLintelCells = 1;

// The whole steps a wall's openings are measured in: its plan offset over their gcd, so every
// step ends on whole cells. Zero where the nodes coincide in the plane.
int WallSteps(const WallGeometry& wall);

// The wall's panels around its openings: the wall beside, above and (under a window) below each,
// and a pane of glass in a window; the seams at the ends go to the first and last. An error for
// a bent or trapezoid wall, openings that overlap or leave less than a step of wall at an end or
// a cell above them, or a door with a sill.
std::expected<std::vector<z13::station::Block>, std::string> OpeningPanels(
    const WallGeometry& wall, const WallEnds& ends, std::span<const z13::station::WallOpening> openings,
    const Palette& palette);

}  // namespace z13::building::primitives
