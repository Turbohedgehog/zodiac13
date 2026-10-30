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

#include <Eigen/Dense>

#include <primitives/placement.h>

namespace z13::station::rooms {

// The walls that end the rooms at the edge of what a world holds (docs/interest-design.md): a layer of cells outside
// each face of `sectors` (cubes of `sector_cells` cells) that none of the others covers, cut to `within`, sorted.
std::vector<z13::building::primitives::CellBox> CutWalls(std::span<const Eigen::Vector3i> sectors, int sector_cells,
                                                         const z13::building::primitives::CellBox& within);

}  // namespace z13::station::rooms
