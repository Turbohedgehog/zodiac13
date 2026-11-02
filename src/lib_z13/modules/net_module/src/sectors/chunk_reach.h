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
#include <span>
#include <unordered_map>
#include <vector>

#include <flecs.h>

#include <sectors/sector.h>

#include "interest_view.h"

namespace z13::net {

// Which sectors the blocks of each chunk (a snapshot group) touch, from the sector index. A client holds a chunk's
// group while the chunk touches one of its sectors, so a block longer than a sector reaches every client that sees
// any part of it (docs/network-scaling-plan.md, f/sector-span).
class ChunkReach {
 public:
  static ChunkReach Of(flecs::world world);

  // The packed chunks (group ids) touching `sector`, sorted.
  std::span<const uint64_t> ChunksIn(const sectors::SectorKey& sector) const;
  bool TouchesAny(uint64_t chunk, const SectorSet& sectors) const;

 private:
  std::unordered_map<uint64_t, std::vector<sectors::SectorKey>> sectors_of_;
  std::unordered_map<sectors::SectorKey, std::vector<uint64_t>, sectors::SectorHash> chunks_in_;
};

}  // namespace z13::net
