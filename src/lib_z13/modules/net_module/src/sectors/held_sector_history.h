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
#include <map>
#include <optional>

#include <flecs.h>

#include <sectors/sector.h>

#include "interest_view.h"

namespace z13::net {

struct SectorChange {
  sectors::SectorKey sector = sectors::SectorKey::Zero();
  bool entered {};
};

// A client's held sectors over time (docs/interest-design.md): what its catch-up gave, then each SectorEntered and
// SectorLeft by its apply tick, so a replayed tick sees the set it had then.
struct HeldSectorHistory {
  using Singleton = void;
  using SessionScoped = void;
  std::optional<SectorSet> base;  // nullopt: the whole world
  std::multimap<uint64_t, SectorChange> changes;
};

void StartHeldSectors(flecs::world world, std::optional<SectorSet> sectors);
void RecordSectorChange(flecs::world world, uint64_t apply_tick, const SectorChange& change);
// Sets z13::station::HeldSectors to the set at this tick, and folds into the base what no rollback goes back to.
void UpdateHeldSectors(flecs::world world);

}  // namespace z13::net
