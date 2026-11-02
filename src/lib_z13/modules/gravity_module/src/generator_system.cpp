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

#include "generator_system.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <tuple>
#include <vector>

#include <lib_core/state/world_serializer.h>
#include <lib_core/state/world_state.h>
#include <lib_core/time/simulation_clock.h>
#include <lib_core/world/lifecycle.h>

#include <primitives/palette.h>
#include <z13/components/block.h>
#include <z13/components/block_change_reader.h>
#include <z13/components/gravity.h>
#include <z13/components/station_mode.h>
#include <z13/components/utilities.h>
#include <z13_settings/physics_tuning.h>

namespace z13::gravity_module {

namespace {

using z13::building::primitives::BlockPalette;
using z13::gravity::GravityCharge;
using z13::gravity::GravityLevel;
using z13::gravity::GravitySource;
using z13::gravity::GravitySources;
using z13::station::Block;
using z13::station::BlockChangeBatch;
using z13::station::BlockChangeKind;
using z13::station::BlockChangeReader;
using z13::station::kMainGrid;

using GeneratorQuery = flecs::query<GravityCharge, const Block>;

bool IsGeneratorBlock(const Block& block, const BlockPalette& palette) {
  const auto primitive = palette.palette.Find(block.spec.type_id);
  return primitive && std::ranges::any_of(primitive->get().ports, [](const auto& port) {
           return port.network == kNetworkKey && port.role == kGeneratorRole;
         });
}

// The generators among the blocks that came this tick, or among all of them after a restore.
void AddCharges(BlockChangeReader& reader, uint64_t restores, uint64_t tick, const BlockPalette& palette,
                const flecs::world& world) {
  const BlockChangeBatch batch = reader.Read(restores, tick);
  const auto add = [&palette](flecs::entity block, const Block& placed) {
    if (!block.has<GravityCharge>() && IsGeneratorBlock(placed, palette)) {
      block.set(GravityCharge {});
    }
  };
  if (batch.resync) {
    reader.Blocks().each(add);
    return;
  }
  for (const z13::station::BlockChange& change : batch.changes) {
    if (change.kind == BlockChangeKind::kAdded && world.is_alive(change.entity)) {
      add(world.entity(change.entity), change.block);
    }
  }
}

GravityLevel Ramp(GravityLevel level, bool powered, uint32_t ramp_ticks) {
  const int step = std::max(static_cast<int>(z13::gravity::kGravityFull / std::max<uint32_t>(ramp_ticks, 1)), 1);
  const int target = powered ? z13::gravity::kGravityFull : 0;
  const int now = level;
  return static_cast<GravityLevel>(target > now ? std::min(now + step, target) : std::max(now - step, target));
}

void Update(GravitySources& sources, const z13::PhysicsTuning& tuning, const GeneratorQuery& generators) {
  std::vector<GravitySource> list;
  generators.each([&](flecs::entity block, GravityCharge& charge, const Block& placed) {
    if (placed.grid != kMainGrid) {
      return;
    }
    charge.level = Ramp(charge.level, z13::utility::IsSupplied(block), tuning.generator_ramp_ticks);
    list.push_back({.cell = placed.cell, .level = charge.level});
  });
  std::ranges::sort(list, [](const GravitySource& a, const GravitySource& b) {
    return std::tie(a.cell.x(), a.cell.y(), a.cell.z()) < std::tie(b.cell.x(), b.cell.y(), b.cell.z());
  });
  if (sources.list != list) {
    sources.list = std::move(list);
  }
}

void RegisterComponents(flecs::world world) {
  z13::flecs_tools::RegisterComponents<GravityCharge>(world);
}

void RegisterSystems(flecs::world world) {
  const auto reader = std::make_shared<BlockChangeReader>(world);
  world
      .system<const BlockPalette, const z13::flecs_tools::WorldRestores, const z13::flecs_tools::SimulationClock>(
          "GeneratorSystem::AddCharges")
      .kind<z13::utility::UtilityConsumersPhase>()
      .with<z13::station::StationMode>()
      .read<Block>()
      .read<z13::station::BlockChange>()
      .write<GravityCharge>()
      .each([reader](flecs::iter& it, size_t, const BlockPalette& palette,
                     const z13::flecs_tools::WorldRestores& restores, const z13::flecs_tools::SimulationClock& clock) {
        AddCharges(*reader, restores.count, clock.tick, palette, it.world());
      });

  const GeneratorQuery generators = world.query<GravityCharge, const Block>();
  world.system<GravitySources, const z13::PhysicsTuning>("GeneratorSystem::Update")
      .kind<z13::utility::UtilityConsumersPhase>()
      .with<z13::station::StationMode>()
      .read<Block>()
      .read<z13::utility::UtilitySupply>()
      .write<GravityCharge>()
      .each([generators](GravitySources& sources, const z13::PhysicsTuning& tuning) {
        Update(sources, tuning, generators);
      });
}

}  // namespace

void GeneratorSystem::Register(flecs::world& world) {
  z13::OnRegisterComponents(world, RegisterComponents);
  z13::OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::gravity_module
