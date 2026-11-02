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

#include <map>
#include <unordered_map>
#include <unordered_set>
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

struct PlayerHash {
  uint32_t player_id {};
  uint64_t hash {};
};

// A sector's hash in parts: its block groups, then each player standing in it by name.
struct SectorHashParts {
  uint64_t blocks {};
  std::vector<PlayerHash> players;
};

struct SplitHashes {
  uint64_t global {};
  std::unordered_map<sectors::SectorKey, SectorHashParts, sectors::SectorHash> by_sector;
};

// Server: the digests' hashes in parts by tick, made with the digest (the sector index moves on before it is sent).
struct DigestSplits {
  using Singleton = void;
  using SessionScoped = void;
  std::map<uint64_t, SplitHashes> by_tick;
};

SplitHashes SplitBySector(flecs::world world, const z13::flecs_tools::WorldSnapshot& snapshot,
                          const sectors::SectorIndex& index, int chunk_cells);

// The sectors' hashes counting only `players` (all of them when null): what a client that holds only some of the
// players in a sector hashes. Sorted by sector.
std::vector<fbs::net::SectorHashWire> HashesOf(const SplitHashes& split, const std::unordered_set<uint32_t>* players);

}  // namespace z13::net
