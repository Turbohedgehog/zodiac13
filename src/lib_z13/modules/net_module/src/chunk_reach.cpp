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

#include "chunk_reach.h"

#include <algorithm>

#include <primitives/placement.h>
#include <sectors/sector_index.h>
#include <z13/components/block.h>
#include <z13_settings/building_tuning.h>

namespace z13::net {

ChunkReach ChunkReach::Of(flecs::world world) {
  const auto& index = world.get<sectors::SectorIndex>();
  const int chunk_cells = world.get<z13::BuildingTuning>().index_chunk_cells;
  ChunkReach reach;
  for (const uint64_t id : index.IdsOf(sectors::MemberKind::kBlock)) {
    const flecs::entity block = world.entity(id);
    if (!block.is_alive() || !block.has<z13::station::Block>()) {
      continue;  // destroyed this tick; the index drops it in its phase
    }
    const uint64_t chunk = z13::building::primitives::PackChunk(
        z13::building::primitives::ChunkOf(block.get<z13::station::Block>().cell, chunk_cells));
    for (const sectors::SectorKey& sector : index.SectorsOf({.kind = sectors::MemberKind::kBlock, .id = id})) {
      reach.sectors_of_[chunk].push_back(sector);
      reach.chunks_in_[sector].push_back(chunk);
    }
  }
  const auto unique = [](auto& values, auto less) {
    std::ranges::sort(values, less);
    values.erase(std::ranges::unique(values).begin(), values.end());
  };
  for (auto& [chunk, touched] : reach.sectors_of_) {
    unique(touched, [](const sectors::SectorKey& a, const sectors::SectorKey& b) {
      return std::ranges::lexicographical_compare(a, b);
    });
  }
  for (auto& [sector, chunks] : reach.chunks_in_) {
    unique(chunks, std::ranges::less {});
  }
  return reach;
}

std::span<const uint64_t> ChunkReach::ChunksIn(const sectors::SectorKey& sector) const {
  const auto found = chunks_in_.find(sector);
  return found == chunks_in_.end() ? std::span<const uint64_t> {} : std::span<const uint64_t> {found->second};
}

bool ChunkReach::TouchesAny(uint64_t chunk, const SectorSet& sectors) const {
  const auto found = sectors_of_.find(chunk);
  return found != sectors_of_.end() && std::ranges::any_of(found->second, [&sectors](const sectors::SectorKey& sector) {
           return sectors.contains(sector);
         });
}

}  // namespace z13::net
