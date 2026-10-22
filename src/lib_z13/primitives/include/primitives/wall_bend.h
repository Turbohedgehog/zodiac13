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
#include <expected>
#include <optional>
#include <string>
#include <vector>

#include <z13/components/block.h>
#include <z13/components/wall.h>
#include <primitives/palette.h>
#include <primitives/triangle_panel.h>
#include <primitives/wall_panel.h>

namespace z13::building::primitives {

// From a stick's node to its tip.
Eigen::Vector3i StickOffset(const z13::station::WallStick& stick);

// Whether the wall's sticks lie along different axes, so that its surface is two triangles.
bool IsBent(const WallGeometry& wall);

// The two triangles of a bent wall along `diagonal`, each wound as the corners a base, a tip,
// b tip, b base go round.
std::array<Triangle, 2> BentTriangles(const WallGeometry& wall, z13::station::WallDiagonal diagonal);

// The diagonal that folds the surface least, comparing the angles between the triangles'
// normals; the one from a's base on a tie.
z13::station::WallDiagonal DefaultDiagonal(const WallGeometry& wall);

z13::station::WallDiagonal Flipped(z13::station::WallDiagonal diagonal);

// The angle in degrees between the normals of the bent wall's two triangles; zero for any other
// wall, and for a flat surface.
double FoldDegrees(const WallGeometry& wall);

// Whether the fold is at most `max_degrees`, decided on the cosine with rounding allowed for.
bool FoldWithin(const WallGeometry& wall, int max_degrees);

// The thicker of the two triangles' least thickness; nothing if one is a line.
std::optional<int> MinBentThickness(const WallGeometry& wall);

// The panel blocks of the wall: one for sticks of one axis, else a triangle on each side of its
// diagonal, both thickened to the side of the surface that `thickness` has the sign of.
std::expected<std::vector<z13::station::Block>, std::string> WallPanelBlocks(
    const WallGeometry& wall, const Palette& palette);

}  // namespace z13::building::primitives
