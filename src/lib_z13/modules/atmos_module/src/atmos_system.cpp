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

#include "atmos_system.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <cstdlib>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <unordered_set>
#include <vector>

#include <flecs.h>

#include <lib_core/state/world_serializer.h>
#include <lib_core/state/world_state.h>
#include <lib_core/time/simulation_clock.h>
#include <lib_core/utils/flecs_utils.h>
#include <lib_core/utils/log.h>
#include <lib_core/utils/math.h>
#include <lib_core/world/lifecycle.h>

#include <atmos/air.h>
#include <atmos/devices.h>
#include <atmos/exchange.h>
#include <atmos/port_names.h>
#include <networks/station_networks.h>
#include <primitives/palette.h>
#include <primitives/placement.h>
#include <rooms/portal_source.h>
#include <rooms/room_cache.h>
#include <z13/components/atmosphere.h>
#include <z13/components/block.h>
#include <z13/components/block_change_reader.h>
#include <z13/components/door.h>
#include <z13/components/grid.h>
#include <z13/components/interaction.h>
#include <z13/components/player.h>
#include <z13/components/station_mode.h>
#include <z13/components/utilities.h>
#include <z13_settings/atmos_tuning.h>
#include <z13_settings/door_tuning.h>

#include "airlock.h"
#include "atmos_queries.h"
#include "door_at.h"
#include "room_air_carry.h"
#include "room_air_store.h"
#include "room_index.h"
#include "rooms_now.h"

namespace z13::atmos_module {

namespace {

using z13::building::primitives::BlockPalette;
using z13::station::Block;
using z13::station::BlockChangeBatch;
using z13::station::BlockChangeReader;
using z13::station::DoorState;
using z13::station::Grid;
using z13::station::kCellSize;
using z13::station::rooms::Portal;
using z13::station::rooms::RoomCache;
using z13::station::rooms::RoomGraph;
using z13::station::rooms::RoomId;
using z13::utility::AirlockCycle;
using z13::utility::AirlockDoors;
using z13::utility::DoorInterlock;
using z13::utility::RoomAirBasis;
using z13::utility::RoomAirEntry;
using z13::utility::networks::StationNetworks;
namespace atmos = z13::utility::atmos;

// A door lets through the share of the portal's area that it is open; a doorway without a door all of it; a window
// or a wall nothing.
int64_t OpenArea(const Portal& portal, std::span<const DoorAt> doors) {
  if (const auto door = DoorAtOpening(doors, portal.opening)) {
    return portal.area_cells * static_cast<int64_t>(door->get().position) / z13::station::kDoorOpen;
  }
  return portal.passable ? portal.area_cells : 0;
}

std::vector<atmos::Link> LinksOf(const RoomGraph& graph, const RoomIndex& index, std::span<const DoorAt> doors) {
  std::vector<atmos::Link> links;
  for (const Portal& portal : graph.portals) {
    const int64_t area = OpenArea(portal, doors);
    const size_t a = ChamberOf(index, portal.a);
    const size_t b = ChamberOf(index, portal.b);
    if (area > 0 && (a != atmos::kOutside || b != atmos::kOutside)) {
      links.push_back({.a = a, .b = b, .area_cells = area});
    }
  }
  return links;
}

std::vector<atmos::Chamber> ChambersOf(const std::vector<RoomAirEntry>& rooms, const RoomGraph& graph,
                                       const RoomIndex& index) {
  std::vector<atmos::Chamber> chambers;
  for (size_t i = 0; i < index.ids.size(); ++i) {
    chambers.push_back({.air = AirOf(rooms[i]), .volume_cells = graph.rooms.at(index.ids[i]).volume_cells});
  }
  return chambers;
}

size_t ChamberAt(const RoomGraph& graph, const RoomIndex& index, const Eigen::Vector3f& position) {
  const auto room = graph.RoomAt((position / kCellSize).array().floor().cast<int>());
  return room ? ChamberOf(index, *room) : atmos::kOutside;
}

void BreatheRooms(std::vector<atmos::Chamber>& chambers, const RoomGraph& graph, const RoomIndex& index,
                  const PlayerQuery& players, const z13::AtmosTuning& tuning) {
  std::vector<int> bodies(chambers.size());
  players.each([&](const z13::gameplay::Player&, const Eigen::Matrix4f& transform) {
    const size_t chamber = ChamberAt(graph, index, z13::math::ExtractTranslation<float>(transform));
    if (chamber != atmos::kOutside) {
      ++bodies[chamber];
    }
  });
  const atmos::Breath breath {.millimoles = tuning.breath_millimoles, .heat = tuning.body_heat_millijoules};
  for (size_t i = 0; i < chambers.size(); ++i) {
    for (int body = 0; body < bodies[i]; ++body) {
      atmos::Breathe(chambers[i].air, breath);
    }
  }
}

// A unit's capacity is shared among the rooms of the vents on its network.
atmos::Regulation Shared(atmos::Regulation regulation, int64_t rooms) {
  regulation.scrub_millimoles /= rooms;
  regulation.fill_millimoles /= rooms;
  regulation.heat_millijoules /= rooms;
  return regulation;
}

bool IsPowered(flecs::entity unit) {
  const auto* supply = unit.try_get<z13::utility::UtilitySupply>();
  return supply != nullptr && supply->satisfied;
}

void RegulateNetwork(const z13::utility::networks::Network& network, const flecs::world& world,
                     const BlockPalette& palette, const RoomGraph& graph, const RoomIndex& index,
                     std::vector<atmos::Chamber>& chambers) {
  std::vector<atmos::Regulation> units;
  std::vector<size_t> vented;
  for (const uint64_t id : network.ports) {
    const flecs::entity block = world.entity(id);
    const Block& placed = block.get<Block>();
    const auto primitive = palette.palette.Find(placed.spec.type_id);
    const auto port = primitive ? atmos::GasPortOf(*primitive) : std::nullopt;
    if (!port) {
      continue;
    }
    if (port->get().role == atmos::kVentRole) {
      const std::vector<size_t> around = ChambersAround(graph, index, z13::building::primitives::OccupiedCells(placed));
      vented.insert(vented.end(), around.begin(), around.end());
    } else if (const auto regulation = atmos::RegulationOf(port->get()); regulation && IsPowered(block)) {
      units.push_back(*regulation);
    }
  }
  std::ranges::sort(vented);
  const auto [unique_end, vented_end] = std::ranges::unique(vented);
  vented.erase(unique_end, vented_end);
  if (vented.empty()) {
    return;
  }
  for (const atmos::Regulation& unit : units) {
    const atmos::Regulation share = Shared(unit, static_cast<int64_t>(vented.size()));
    for (const size_t chamber : vented) {
      auto& vented_chamber = chambers[chamber];
      atmos::Regulate(vented_chamber.air, vented_chamber.volume_cells, share);
    }
  }
}

void RegulateRooms(const StationNetworks& stations, const flecs::world& world, const BlockPalette& palette,
                   const RoomGraph& graph, const RoomIndex& index, std::vector<atmos::Chamber>& chambers) {
  for (const auto& network : stations.networks.All()) {
    if (network.key == atmos::kNetworkKey) {
      RegulateNetwork(network, world, palette, graph, index, chambers);
    }
  }
}

int64_t DifferenceAt(const DoorAt& door, const RoomGraph& graph, const RoomIndex& index,
                     const std::vector<atmos::Chamber>& chambers) {
  int64_t greatest = 0;
  for (const Portal& portal : graph.portals) {
    if (Overlaps(door, portal.opening)) {
      const int64_t difference =
          atmos::PressureOf(chambers, ChamberOf(index, portal.a)) -
          atmos::PressureOf(chambers, ChamberOf(index, portal.b));
      greatest = std::max(greatest, std::abs(difference));
    }
  }
  return greatest;
}

// Locked while the difference is great: an automatic door does not open into a room that would empty through it.
// The doors of airlocks are held by their controllers instead.
void LockDoors(std::span<const DoorAt> doors, const std::unordered_set<flecs::entity_t>& held, const RoomGraph& graph,
               const RoomIndex& index, const std::vector<atmos::Chamber>& chambers, const z13::AtmosTuning& tuning) {
  for (const DoorAt& door : doors) {
    if (held.contains(door.entity.id())) {
      continue;
    }
    const bool locked = DifferenceAt(door, graph, index, chambers) > tuning.interlock_pascals;
    if (locked && !door.entity.has<DoorInterlock>()) {
      door.entity.set(DoorInterlock {.reason = z13::utility::InterlockReason::kPressure});
    } else if (!locked && door.entity.has<DoorInterlock>()) {
      door.entity.remove<DoorInterlock>();
    }
  }
}

void FollowRooms(RoomAirBasis& basis, std::vector<RoomAirEntry>& rooms, const RoomCache& cache,
                 const RoomGraph& graph) {
  const uint64_t fingerprint = cache.CurrentFingerprint().value_or(0);
  if (!basis.seeded) {
    if (!graph.ByAnchor().empty()) {
      basis = {.fingerprint = fingerprint, .seeded = true};
      rooms = SeedRooms(graph);
    }
    return;
  }
  // The graph before the switch is still cached, the air having followed every switch; without it the rooms keep
  // what has their anchor.
  const auto before = cache.Cached(basis.fingerprint);
  rooms = before ? CarryAir(rooms, *before, graph) : KeepAirByAnchor(rooms, graph);
  basis.fingerprint = fingerprint;
}

void Step(std::vector<RoomAirEntry>& rooms, const RoomsNow& now, const std::unordered_set<flecs::entity_t>& held,
          const StationNetworks& stations, const BlockPalette& palette, const z13::AtmosTuning& tuning,
          const z13::DoorTuning& door_tuning, const flecs::world& world, const PlayerQuery& players) {
  const RoomGraph& graph = now.graph;
  std::vector<atmos::Chamber> chambers = ChambersOf(rooms, graph, now.index);

  std::vector<atmos::Link> links = LinksOf(graph, now.index, now.doors);
  AddPumpLinks(links, now.airlocks, tuning);
  atmos::Exchange(chambers, links, {.coefficient = tuning.flow_coefficient, .max_outflow_ppm = tuning.max_outflow_ppm});
  BreatheRooms(chambers, graph, now.index, players, tuning);
  RegulateRooms(stations, world, palette, graph, now.index, chambers);
  FinishCycles(now.airlocks, chambers, tuning, door_tuning);
  LockDoors(now.doors, held, graph, now.index, chambers, tuning);

  for (size_t i = 0; i < chambers.size(); ++i) {
    auto& room = rooms[i];
    room = EntryOf(room.anchor, chambers[i].air);
  }
}

void RegisterComponents(flecs::world world) {
  z13::flecs_tools::RegisterComponents<RoomAirBasis, RoomAirEntry, DoorInterlock, AirlockCycle, AirlockDoors>(world);
}

void Update(flecs::iter& it, const AtmosQueries& queries, const StationNetworks& stations, const BlockPalette& palette,
            const z13::AtmosTuning& tuning, const z13::DoorTuning& door_tuning,
            const z13::station::Interactions& interactions, const z13::flecs_tools::SimulationClock& clock) {
  flecs::world world = it.world();
  const flecs::entity grid = z13::station::MainGridEntity(world);
  const auto cache = z13::station::MainGridData<RoomCache>(world);
  const auto graph = cache ? cache->get().Current() : std::nullopt;
  if (!grid.is_valid() || !graph) {
    return;
  }
  if (!grid.has<RoomAirBasis>()) {
    const z13::ImmediateScope immediate(world);
    grid.set(RoomAirBasis {});
  }
  RoomAirBasis& basis = grid.get_mut<RoomAirBasis>();
  const bool follows = basis.seeded && basis.fingerprint == cache->get().CurrentFingerprint() &&
                       static_cast<size_t>(queries.stored.count()) == graph->get().ByAnchor().size();
  const bool step = basis.seeded && clock.tick % tuning.step_ticks == 0;
  RoomsNow now {.graph = graph->get()};
  if (!follows || step) {
    now.index = IndexOf(now.graph);
    now.doors = DoorsOf(queries.doors, palette);
    now.airlocks = FindAirlocks(queries.airlocks, world, now.graph, now.index, now.doors);
  }
  // Every tick, so that a use of an airlock's door is never missed between two steps of the air.
  const std::unordered_set<flecs::entity_t> held =
      TendAirlocks(queries.airlocks, queries.interlocks, interactions, world);
  if (follows && !step) {
    return;
  }
  std::vector<RoomAirEntry> rooms = LoadRoomAir(queries.stored);
  if (!follows) {
    FollowRooms(basis, rooms, cache->get(), graph->get());
  }
  if (basis.seeded && step) {
    Step(rooms, now, held, stations, palette, tuning, door_tuning, world, queries.players);
  }
  StoreRoomAir(world, queries.stored, rooms);
}

bool IsAirlockBlock(const Block& block, const BlockPalette& palette) {
  const auto primitive = palette.palette.Find(block.spec.type_id);
  const auto port = primitive ? atmos::GasPortOf(primitive->get()) : std::nullopt;
  return port && port->get().role == atmos::kAirlockRole;
}

// The airlock controllers among the blocks that came this tick, or among all of them after a restore.
void AddAirlocks(BlockChangeReader& reader, uint64_t restores, uint64_t tick, const BlockPalette& palette,
                 const flecs::world& world) {
  const BlockChangeBatch batch = reader.Read(restores, tick);
  const auto add = [&palette](flecs::entity block, const Block& placed) {
    if (!block.has<AirlockCycle>() && IsAirlockBlock(placed, palette)) {
      block.set(AirlockCycle {});
    }
  };
  if (batch.resync) {
    reader.Blocks().each(add);
    return;
  }
  for (const z13::station::BlockChange& change : batch.changes) {
    if (change.kind == z13::station::BlockChangeKind::kAdded && world.is_alive(change.entity)) {
      add(world.entity(change.entity), change.block);
    }
  }
}

void RegisterPipeline(flecs::world world) {
  // The airlocks act on what the players did this tick.
  world.component<z13::utility::UtilitySimulationPhase>().add(flecs::Phase).depends_on<z13::station::InteractionPhase>();
}

void RegisterSystems(flecs::world world) {
  if (const auto* palette = world.try_get<BlockPalette>()) {
    for (const auto& primitive : palette->palette.primitives) {
      for (const auto& port : primitive.ports) {
        if (port.network != atmos::kNetworkKey) {
          continue;
        }
        if (const auto problem = atmos::ValidatePort(port)) {
          log_error("AtmosSystem: primitive {} '{}': {}", primitive.id, primitive.name, *problem);
        }
      }
    }
  }
  world.entity("GasNetwork").set(z13::utility::UtilityProvider {.network = std::string(atmos::kNetworkKey)});

  const AtmosQueries queries {.doors = world.query<const DoorState, const Block>(),
                              .players = world.query<const z13::gameplay::Player, const Eigen::Matrix4f>(),
                              .stored = world.query<const RoomAirEntry>(),
                              .airlocks = world.query<const AirlockCycle, const Block>(),
                              .interlocks = world.query<const DoorInterlock>()};
  const auto reader = std::make_shared<BlockChangeReader>(world);
  world
      .system<const BlockPalette, const z13::flecs_tools::WorldRestores, const z13::flecs_tools::SimulationClock>(
          "AtmosSystem::AddAirlocks")
      .kind<z13::utility::UtilitySimulationPhase>()
      .with<z13::station::StationMode>()
      .read<Block>()
      .read<z13::station::BlockChange>()
      .write<AirlockCycle>()
      .each([reader](flecs::iter& it, size_t, const BlockPalette& palette,
                     const z13::flecs_tools::WorldRestores& restores, const z13::flecs_tools::SimulationClock& clock) {
        AddAirlocks(*reader, restores.count, clock.tick, palette, it.world());
      });
  world
      .system<const StationNetworks, const BlockPalette, const z13::AtmosTuning, const z13::DoorTuning,
              const z13::station::Interactions, const z13::flecs_tools::SimulationClock>("AtmosSystem::Update")
      .kind<z13::utility::UtilitySimulationPhase>()
      .with<z13::station::StationMode>()
      // Immediate, so a grid new to the air gets its component at once (ImmediateScope).
      .immediate()
      .read<Block>()
      .read<DoorState>()
      .read<z13::utility::UtilitySupply>()
      .read<RoomCache>()
      .write<RoomAirBasis>()
      .write<RoomAirEntry>()
      .write<DoorInterlock>()
      .write<DoorState>()
      .write<AirlockCycle>()
      .write<AirlockDoors>()
      .each([queries](flecs::iter& it, size_t, const StationNetworks& stations, const BlockPalette& palette,
                      const z13::AtmosTuning& tuning, const z13::DoorTuning& door_tuning,
                      const z13::station::Interactions& interactions,
                      const z13::flecs_tools::SimulationClock& clock) {
        Update(it, queries, stations, palette, tuning, door_tuning, interactions, clock);
      });
}

}  // namespace

void AtmosSystem::Register(flecs::world& world) {
  z13::OnRegisterComponents(world, RegisterComponents);
  z13::OnInitPhases(world, RegisterPipeline);
  z13::OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::atmos_module
