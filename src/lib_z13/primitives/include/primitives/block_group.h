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
#include <optional>

#include <Eigen/Core>

#include <z13/components/station/grid_id.h>

namespace z13::building::primitives {

// The snapshot group of a block: a chunk of the main grid (PackChunk, so the id is the same as before grids), or
// all the blocks of another grid in one group, flagged. A district reaches a client as a whole, by every sector it
// touches (docs/multi-grid-plan.md).
uint64_t BlockGroupId(z13::station::GridId grid, const Eigen::Vector3i& chunk);

// The grid whose blocks the group holds; nullopt for a chunk of the main grid.
std::optional<z13::station::GridId> GridOfGroup(uint64_t group);

}  // namespace z13::building::primitives
