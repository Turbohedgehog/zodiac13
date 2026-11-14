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
#include <tuple>
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
#include <z13/components/station/block_change_reader.h>
#include <z13/components/station/conduit.h>
#include <z13/components/station/dock.h>
#include <z13/components/station/station_mode.h>
#include <z13/components/station/switch.h>
#include <z13/components/utility/utilities.h>

namespace z13::conduit {

namespace {

using z13::station::Block;
using z13::station::BlockChangeReader;
using z13::utility::networks::StationNetworks;

// What the last build used, to tell when the next tick needs another.
struct BuildInputs {
  std::vector<Conduit> conduits;
  std::vector<z13::station::Dock> docks;
  // The switches that were on.
  std::vector<flecs::entity_t> closed;
  uint64_t restores {};
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

// A dock joins the networks of its two grids, as a cable through its door would: one link for each network.
std::vector<z13::utility::networks::Link> LinksOf(const std::vector<z13::station::Dock>& docks,
                                                  const ConduitKinds& kinds) {
  std::vector<std::string> networks;
  for (const ConduitKind& kind : kinds.kinds) {
    if (!std::ranges::contains(networks, kind.network)) {
      networks.push_back(kind.network);
    }
  }
  std::vector<z13::utility::networks::Link> links;
  for (const z13::station::Dock& dock : docks) {
    for (const std::string& network : networks) {
      links.push_back({.network = network,
                       .a_grid = dock.a.grid,
                       .a_cell = dock.a.cell,
                       .b_grid = dock.b.grid,
                       .b_cell = dock.b.cell});
    }
  }
  return links;
}

std::vector<z13::station::Dock> SortedDocks(const flecs::query<const z13::station::Dock>& query) {
  std::vector<z13::station::Dock> docks;
  query.each([&docks](const z13::station::Dock& dock) { docks.push_back(dock); });
  const auto key = [](const z13::station::Dock& dock) {
    return std::make_tuple(dock.a.grid, dock.a.cell.x(), dock.a.cell.y(), dock.a.cell.z(), dock.b.grid,
                           dock.b.cell.x(), dock.b.cell.y(), dock.b.cell.z());
  };
  std::ranges::sort(docks, [&key](const auto& a, const auto& b) { return key(a) < key(b); });
  return docks;
}

z13::utility::networks::Networks Build(const flecs::query<const Conduit>& conduits,
                                       const flecs::query<const Block>& blocks,
                                       const std::vector<z13::station::Dock>& docks, const ConduitKinds& kinds,
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
                       .grid = block.grid,
                       .cells = z13::building::primitives::OccupiedCells(block)});
    }
  });
  return z13::utility::networks::BuildNetworks(runs, ports, LinksOf(docks, kinds));
}

void RegisterComponents(flecs::world world) {
  z13::flecs_tools::RegisterComponent<StationNetworks>(world);
}

void RegisterSystems(flecs::world world) {
  // Set here, not next to `.add(flecs::Singleton)` (see PhysicsSystem::RegisterSystems).
  world.set(StationNetworks {});

  const auto reader = std::make_shared<BlockChangeReader>(world);
  const auto inputs = std::make_shared<BuildInputs>();
  const auto conduits = world.query_builder<const Conduit>().detect_changes().build();
  const auto switches = world.query_builder<const z13::station::SwitchState>().detect_changes().build();
  const auto docks = world.query_builder<const z13::station::Dock>().detect_changes().build();
  world.system<StationNetworks, const ConduitKinds, const z13::building::primitives::BlockPalette,
               const z13::flecs_tools::WorldRestores, const z13::flecs_tools::SimulationClock>(
           "NetworkSystem::Update")
      .kind<z13::utility::UtilityNetworksPhase>()
      .with<z13::station::StationMode>()
      .read<Block>()
      .read<Conduit>()
      .read<z13::station::SwitchState>()
      .read<z13::station::Dock>()
      .read<z13::station::BlockChange>()
      .each([reader, inputs, conduits, switches, docks](flecs::iter&, size_t, StationNetworks& stations,
                                                 const ConduitKinds& kinds,
                                       const z13::building::primitives::BlockPalette& palette,
                                       const z13::flecs_tools::WorldRestores& restores,
                                       const z13::flecs_tools::SimulationClock& clock) {
        const z13::station::BlockChangeBatch batch = reader->Read(restores.count, clock.tick);
        const bool port_changed =
            batch.resync || std::ranges::any_of(batch.changes, [&palette](const z13::station::BlockChange& change) {
              return HasPort(palette, change.block);
            });
        const bool inputs_touched = conduits.changed() || switches.changed() || docks.changed();
        if (inputs->built && !port_changed && !inputs_touched && inputs->restores == restores.count) {
          return;
        }
        std::vector<Conduit> sorted = SortedConduits(conduits);
        std::vector<z13::station::Dock> sorted_docks = SortedDocks(docks);
        std::vector<flecs::entity_t> closed = ClosedSwitches(switches);
        if (inputs->built && !port_changed && sorted == inputs->conduits && sorted_docks == inputs->docks &&
            closed == inputs->closed) {
          inputs->restores = restores.count;
          return;
        }
        *inputs = {.conduits = std::move(sorted),
                   .docks = std::move(sorted_docks),
                   .closed = std::move(closed),
                   .restores = restores.count,
                   .built = true};
        stations.networks = Build(conduits, reader->Blocks(), inputs->docks, kinds, palette);
        ++stations.generation;
      });
}

}  // namespace

void NetworkSystem::Register(flecs::world& world) {
  z13::OnRegisterComponents(world, RegisterComponents);
  z13::OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::conduit
