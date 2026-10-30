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

#include "held_sector_history.h"

#include <algorithm>
#include <array>
#include <utility>
#include <vector>

#include <lib_core/time/simulation_clock.h>
#include <z13/components/held_sectors.h>
#include <z13_settings/net_tuning.h>

#include "remote_input.h"
#include "station_bounds_log.h"

namespace z13::net {

namespace {

void Apply(const SectorChange& change, SectorSet& sectors) {
  if (change.entered) {
    sectors.insert(change.sector);
  } else {
    sectors.erase(change.sector);
  }
}

}  // namespace

void StartHeldSectors(flecs::world world, std::optional<SectorSet> sectors) {
  world.set(HeldSectorHistory {.base = std::move(sectors)});
}

void RecordSectorChange(flecs::world world, uint64_t apply_tick, const SectorChange& change) {
  auto& history = world.get_mut<HeldSectorHistory>();
  if (history.base) {
    history.changes.emplace(apply_tick, change);
  }
}

void UpdateHeldSectors(flecs::world world) {
  if (!world.has<z13::station::HeldSectors>()) {
    return;  // no station_module in this world
  }
  auto& history = world.get_mut<HeldSectorHistory>();
  z13::station::HeldSectors held {.sector_cells = world.get<NetTuning>().sector_size_cells};
  if (history.base) {
    auto& bounds = world.get_mut<StationBoundsLog>().by_tick;
    const uint64_t tick = world.get<z13::flecs_tools::SimulationClock>().tick;
    if (const std::optional<uint64_t> oldest = OldestSnapshotTick(world)) {
      SettleBounds(bounds, *oldest);
      const auto settled = history.changes.upper_bound(*oldest);
      std::for_each(history.changes.begin(), settled, [&](const auto& entry) { Apply(entry.second, *history.base); });
      history.changes.erase(history.changes.begin(), settled);
    }
    SectorSet sectors = *history.base;
    const auto later = history.changes.upper_bound(tick);
    std::for_each(history.changes.begin(), later, [&](const auto& entry) { Apply(entry.second, sectors); });
    held.sectors.emplace(sectors.begin(), sectors.end());
    std::ranges::sort(*held.sectors, [](const sectors::SectorKey& a, const sectors::SectorKey& b) {
      return std::ranges::lexicographical_compare(std::array {a.x(), a.y(), a.z()}, std::array {b.x(), b.y(), b.z()});
    });
    held.station = BoundsAt(bounds, tick);
  }
  if (world.get<z13::station::HeldSectors>() != held) {
    world.set(std::move(held));
  }
}

}  // namespace z13::net
