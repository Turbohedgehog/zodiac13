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

#include <vector>

#include <flecs.h>

#include <lib_core/state/world_serializer.h>
#include <net_generated.h>

#include <sectors/sector_index.h>

namespace z13::net {

// A snapshot's hash split by sector (docs/interest-design.md): a player counts in the sector of
// its position, a block group in the sector of its chunk. What lies in no sector (the clock, the
// session's singletons, a player the index doesn't know yet) is `global`.
struct SectorHashes {
  uint64_t global {};
  std::vector<fbs::net::SectorHashWire> by_sector;  // sorted by sector
};

SectorHashes HashBySector(flecs::world world, const z13::flecs_tools::WorldSnapshot& snapshot,
                          const sectors::SectorIndex& index, int chunk_cells);

}  // namespace z13::net
