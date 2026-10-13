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

#include <span>
#include <vector>

#include <primitives/placement.h>
#include <rooms/portal_source.h>

namespace z13::station::rooms {

// Doorways and windows without a block in them: a box-shaped hole one cell thick in a wall or
// floor, no wider than `max_cells` along either side, free on both sides, sealed all round
// in its plane, and held on two opposite sides by thin wall (cells free on both sides). Rooms
// are split at such a hole, and it is a visible, passable portal. A corridor one cell wide or
// a gap by the shell, a perpendicular wall or a slope isn't one: what holds it isn't thin.
//
// The holes are those of each plane's union of sealed cross-sections, in the planes of the
// thin `walls`. `sealed` must hold every sealed box within max_cells + 2 cells of the holes
// sought, or a hole is seen where a box is missing. Sorted by PortalSourceBefore.
std::vector<PortalSource> FindOpenings(std::span<const z13::building::primitives::CellBox> sealed,
                                       std::span<const z13::building::primitives::CellBox> walls, int max_cells);

}  // namespace z13::station::rooms
