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


#include "network_system.h"

#include <algorithm>
#include <memory>
#include <vector>

#include <flecs.h>

#include <lib_core/state/world_serializer.h>
#include <lib_core/time/simulation_clock.h>
#include <lib_core/state/world_state.h>
#include <lib_core/world/lifecycle.h>

#include <networks/station_networks.h>
#include <primitives_generated.h>
#include <primitives/palette.h>
#include <primitives/placement.h>
#include <z13/components/block_change_reader.h>
#include <z13/components/conduit.h>
#include <z13/components/station_mode.h>
#include <z13/components/switch.h>
#include <z13/components/utilities.h>

namespace z13::conduit {

namespace {

using z13::station::Block;
using z13::station::BlockChangeReader;
using z13::utility::networks::StationNetworks;

// What the last build used, to tell when the next tick needs another.
struct BuildInputs {
  std::vector<Conduit> conduits;
  // The switches that were on.
  std::vector<flecs::entity_t> closed;
  bool built {};
};

bool HasPort(const z13::building::primitives::BlockPalette& palette, const Block& block) {
  const auto primitive = palette.palette.Find(block.spec.type_id);
  return primitive && !primitive->get().ports.empty();
}

// A switch block lets the network through only while it is on.
bool Conducts(flecs::entity block, const z13::building::primitives::Primitive& primitive) {
  if (!primitive.Has(z13::building::primitives::PrimitiveFlags::Switch)) {
    return true;
  }
  const auto* state = block.try_get<z13::station::SwitchState>();
  return state != nullptr && state->on;
}

std::vector<flecs::entity_t> ClosedSwitches(const flecs::query<const z13::station::SwitchState>& switches) {
  std::vector<flecs::entity_t> closed;
  switches.each([&closed](flecs::entity entity, const z13::station::SwitchState& state) {
    if (state.on) {
      closed.push_back(entity.id());
    }
  });
  std::ranges::sort(closed);
  return closed;
}

z13::utility::networks::Networks Build(const flecs::query<const Conduit>& conduits,
                                       const flecs::query<const Block>& blocks, const ConduitKinds& kinds,
                                       const z13::building::primitives::BlockPalette& palette) {
  std::vector<z13::utility::networks::ConduitRun> runs;
  conduits.each([&](flecs::entity entity, const Conduit& conduit) {
    if (const auto kind = kinds.Find(conduit.kind)) {
      runs.push_back({.id = entity.id(), .network = kind->network, .conduit = conduit});
    }
  });
  std::vector<z13::utility::networks::Port> ports;
  blocks.each([&](flecs::entity entity, const Block& block) {
    const auto primitive = palette.palette.Find(block.spec.type_id);
    if (!primitive || !Conducts(entity, *primitive)) {
      return;
    }
    for (const auto& port : primitive->get().ports) {
      ports.push_back({.id = entity.id(),
                       .network = port.network,
                       .cells = z13::building::primitives::OccupiedCells(block)});
    }
  });
  return z13::utility::networks::BuildNetworks(runs, ports);
}

void RegisterComponents(flecs::world world) {
  z13::flecs_tools::RegisterComponent<StationNetworks>(world);
}

void RegisterSystems(flecs::world world) {
  // Set here, not next to `.add(flecs::Singleton)` (see PhysicsSystem::RegisterSystems).
  world.set(StationNetworks {});

  const auto reader = std::make_shared<BlockChangeReader>(world);
  const auto inputs = std::make_shared<BuildInputs>();
  const auto conduits = world.query<const Conduit>();
  const auto switches = world.query<const z13::station::SwitchState>();
  world.system<StationNetworks, const ConduitKinds, const z13::building::primitives::BlockPalette,
               const z13::flecs_tools::WorldRestores, const z13::flecs_tools::SimulationClock>(
           "NetworkSystem::Update")
      .kind<z13::utility::UtilityNetworksPhase>()
      .with<z13::station::StationMode>()
      .read<Block>()
      .read<Conduit>()
      .read<z13::station::SwitchState>()
      .read<z13::station::BlockChange>()
      .each([reader, inputs, conduits, switches](flecs::iter&, size_t, StationNetworks& stations,
                                                 const ConduitKinds& kinds,
                                       const z13::building::primitives::BlockPalette& palette,
                                       const z13::flecs_tools::WorldRestores& restores,
                                       const z13::flecs_tools::SimulationClock& clock) {
        const z13::station::BlockChangeBatch batch = reader->Read(restores.count, clock.tick);
        const bool port_changed =
            batch.resync || std::ranges::any_of(batch.changes, [&palette](const z13::station::BlockChange& change) {
              return HasPort(palette, change.block);
            });
        std::vector<Conduit> sorted = SortedConduits(conduits);
        std::vector<flecs::entity_t> closed = ClosedSwitches(switches);
        if (inputs->built && !port_changed && sorted == inputs->conduits && closed == inputs->closed) {
          return;
        }
        *inputs = {.conduits = std::move(sorted), .closed = std::move(closed), .built = true};
        stations.networks = Build(conduits, reader->Blocks(), kinds, palette);
        ++stations.generation;
      });
}

}  // namespace

void NetworkSystem::Register(flecs::world& world) {
  z13::OnRegisterComponents(world, RegisterComponents);
  z13::OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::conduit
