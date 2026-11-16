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
#include <iterator>
#include <unordered_map>
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
#include <z13/components/utility/atmosphere.h>
#include <z13/components/station/block.h>
#include <z13/components/station/block_change_reader.h>
#include <z13/components/station/dock.h>
#include <z13/components/station/door.h>
#include <z13/components/station/grid.h>
#include <z13/components/station/grid_pose.h>
#include <z13/components/station/interaction.h>
#include <z13/components/gameplay/player.h>
#include <z13/components/station/station_mode.h>
#include <z13/components/utility/utilities.h>
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
using z13::station::DockLinks;
using z13::station::Grid;
using z13::station::GridToWorld;
using z13::station::kMainGrid;
using z13::station::kCellSize;
using z13::station::rooms::Portal;
using z13::station::rooms::RoomCache;
using z13::station::rooms::RoomGraph;
using z13::station::rooms::RoomId;
using z13::utility::AirlockCycle;
using z13::utility::AirlockDoors;
using z13::station::DoorInterlock;
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

using DoorIds = std::unordered_set<flecs::entity_t>;

// The links of one grid's portals. The vacuum a dock door opens into is the dock's other side, which the dock link
// joins, so the door lets nothing out into it.
std::vector<atmos::Link> LinksOf(const GridAir& grid, const DoorIds& dock_doors) {
  std::vector<atmos::Link> links;
  for (const Portal& portal : grid.graph.get().portals) {
    const int64_t area = OpenArea(portal, grid.doors);
    const size_t a = ChamberOf(grid.index, portal.a);
    const size_t b = ChamberOf(grid.index, portal.b);
    const auto door = DoorAtOpening(grid.doors, portal.opening);
    const bool to_dock = door && dock_doors.contains(door->get().entity.id());
    if (area > 0 && (a != atmos::kOutside || b != atmos::kOutside) &&
        !(to_dock && (a == atmos::kOutside || b == atmos::kOutside))) {
      links.push_back({.a = a == atmos::kOutside ? a : a + grid.first,
                       .b = b == atmos::kOutside ? b : b + grid.first,
                       .area_cells = area});
    }
  }
  return links;
}

std::optional<size_t> ChamberOfRef(std::span<const GridAir> grids, const z13::station::RoomRef& ref) {
  const auto grid = std::ranges::find(grids, ref.grid, &GridAir::grid);
  if (grid == grids.end()) {
    return std::nullopt;
  }
  const size_t chamber = ChamberOf(grid->index, ref.room);
  return chamber == atmos::kOutside ? std::nullopt : std::optional(chamber + grid->first);
}

// What a dock lets through: the cells of the smaller of its two doors' openings as far as they are open; a dock
// without a door lets nothing through.
std::optional<int64_t> DockArea(const z13::station::DockLink& link, const flecs::world& world) {
  std::optional<int64_t> area;
  for (const z13::station::DockDoor& side : {link.door_a, link.door_b}) {
    const auto* state = side.door != 0 && world.is_alive(side.door) ? world.entity(side.door).try_get<DoorState>()
                                                                     : nullptr;
    if (state == nullptr) {
      continue;
    }
    const int64_t open = side.area_cells * static_cast<int64_t>(state->position) / z13::station::kDoorOpen;
    area = area ? std::min(*area, open) : open;
  }
  return area;
}

void AddDockLinks(std::vector<atmos::Link>& links, std::span<const GridAir> grids, const DockLinks& docks,
                  const flecs::world& world) {
  for (const z13::station::DockLink& dock : docks.list) {
    const auto a = ChamberOfRef(grids, dock.a);
    const auto b = ChamberOfRef(grids, dock.b);
    const auto area = DockArea(dock, world);
    if (a && b && area && *area > 0) {
      links.push_back({.a = *a, .b = *b, .area_cells = *area});
    }
  }
}

DoorIds DockDoorsOf(const DockLinks& docks) {
  DoorIds doors;
  for (const z13::station::DockLink& dock : docks.list) {
    doors.insert({dock.door_a.door, dock.door_b.door});
  }
  doors.erase(0);
  return doors;
}

std::vector<atmos::Chamber> ChambersOf(const std::vector<RoomAirEntry>& rooms, std::span<const GridAir> grids) {
  std::vector<atmos::Chamber> chambers;
  for (const GridAir& grid : grids) {
    for (size_t i = 0; i < grid.index.ids.size(); ++i) {
      chambers.push_back({.air = AirOf(rooms[grid.first + i]),
                          .volume_cells = grid.graph.get().rooms.at(grid.index.ids[i]).volume_cells});
    }
  }
  return chambers;
}

// The chamber holding a body at `position` (the world's meters), in whichever grid.
size_t ChamberAt(std::span<const GridAir> grids, const Eigen::Vector3f& position) {
  for (const GridAir& grid : grids) {
    const Eigen::Vector3f cell = (grid.from_world * position) / kCellSize;
    const auto room = grid.graph.get().RoomAt(cell.array().floor().cast<int>());
    const size_t chamber = room ? ChamberOf(grid.index, *room) : atmos::kOutside;
    if (chamber != atmos::kOutside) {
      return chamber + grid.first;
    }
  }
  return atmos::kOutside;
}

void BreatheRooms(std::vector<atmos::Chamber>& chambers, std::span<const GridAir> grids, const PlayerQuery& players,
                  const z13::AtmosTuning& tuning) {
  std::vector<int> bodies(chambers.size());
  players.each([&](const z13::gameplay::Player&, const Eigen::Matrix4f& transform) {
    const size_t chamber = ChamberAt(grids, z13::math::ExtractTranslation<float>(transform));
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
                     const BlockPalette& palette, const GridAir& main, std::vector<atmos::Chamber>& chambers) {
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
      const std::vector<size_t> around = ChambersAround(main.graph, main.index, z13::building::primitives::OccupiedCells(placed));
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

// Only the main grid has networks, so its units and vents are the ones regulating.
void RegulateRooms(const StationNetworks& stations, const flecs::world& world, const BlockPalette& palette,
                   const GridAir& main, std::vector<atmos::Chamber>& chambers) {
  for (const auto& network : stations.networks.All()) {
    if (network.key == atmos::kNetworkKey) {
      RegulateNetwork(network, world, palette, main, chambers);
    }
  }
}

int64_t DifferenceAt(const DoorAt& door, const GridAir& grid, const std::vector<atmos::Chamber>& chambers) {
  int64_t greatest = 0;
  const auto pressure = [&](z13::station::rooms::RoomId room) {
    const size_t chamber = ChamberOf(grid.index, room);
    return atmos::PressureOf(chambers, chamber == atmos::kOutside ? chamber : chamber + grid.first);
  };
  for (const Portal& portal : grid.graph.get().portals) {
    if (Overlaps(door, portal.opening)) {
      greatest = std::max(greatest, std::abs(pressure(portal.a) - pressure(portal.b)));
    }
  }
  return greatest;
}

// A dock door sees the pressure of the room across the dock, not the vacuum its own grid has beyond the frame.
std::unordered_map<flecs::entity_t, int64_t> DockDifferences(std::span<const GridAir> grids, const DockLinks& docks,
                                                             const std::vector<atmos::Chamber>& chambers) {
  std::unordered_map<flecs::entity_t, int64_t> differences;
  for (const z13::station::DockLink& dock : docks.list) {
    const auto a = ChamberOfRef(grids, dock.a);
    const auto b = ChamberOfRef(grids, dock.b);
    if (!a || !b) {
      continue;
    }
    const int64_t difference = std::abs(atmos::PressureOf(chambers, *a) - atmos::PressureOf(chambers, *b));
    for (const flecs::entity_t door : {dock.door_a.door, dock.door_b.door}) {
      differences[door] = std::max(differences[door], difference);
    }
  }
  return differences;
}

// Locked while the difference is great: an automatic door does not open into a room that would empty through it.
// The doors of airlocks are held by their controllers instead.
void LockDoors(std::span<const GridAir> grids, const DockLinks& docks, const std::unordered_set<flecs::entity_t>& held,
               const std::vector<atmos::Chamber>& chambers, const z13::AtmosTuning& tuning) {
  const auto dock_differences = DockDifferences(grids, docks, chambers);
  for (const GridAir& grid : grids) {
    for (const DoorAt& door : grid.doors) {
      if (held.contains(door.entity.id())) {
        continue;
      }
      const auto dock = dock_differences.find(door.entity.id());
      const int64_t difference = dock != dock_differences.end() ? dock->second : DifferenceAt(door, grid, chambers);
      const bool locked = difference > tuning.interlock_pascals;
      if (locked && !door.entity.has<DoorInterlock>()) {
        door.entity.set(DoorInterlock {.reason = z13::station::InterlockReason::kPressure});
      } else if (!locked && door.entity.has<DoorInterlock>()) {
        door.entity.remove<DoorInterlock>();
      }
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
          const z13::DoorTuning& door_tuning, const DockLinks& docks, const flecs::world& world,
          const PlayerQuery& players) {
  std::vector<atmos::Chamber> chambers = ChambersOf(rooms, now.grids);

  std::vector<atmos::Link> links;
  const DoorIds dock_doors = DockDoorsOf(docks);
  for (const GridAir& grid : now.grids) {
    std::ranges::move(LinksOf(grid, dock_doors), std::back_inserter(links));
  }
  AddDockLinks(links, now.grids, docks, world);
  AddPumpLinks(links, now.airlocks, tuning);
  atmos::Exchange(chambers, links, {.coefficient = tuning.flow_coefficient, .max_outflow_ppm = tuning.max_outflow_ppm});
  BreatheRooms(chambers, now.grids, players, tuning);
  RegulateRooms(stations, world, palette, now.Main(), chambers);
  FinishCycles(now.airlocks, chambers, tuning, door_tuning);
  LockDoors(now.grids, docks, held, chambers, tuning);

  for (size_t i = 0; i < chambers.size(); ++i) {
    auto& room = rooms[i];
    const z13::station::GridId grid = room.grid;
    room = EntryOf(room.anchor, chambers[i].air);
    room.grid = grid;
  }
}

void RegisterComponents(flecs::world world) {
  z13::flecs_tools::RegisterComponents<RoomAirBasis, RoomAirEntry, DoorInterlock, AirlockCycle, AirlockDoors>(world);
}

struct RoomsOfAGrid {
  flecs::entity entity;
  z13::station::GridId id {};
  std::reference_wrapper<const RoomCache> cache;
  std::reference_wrapper<const RoomGraph> graph;
};

// The grids that have a room graph, the main one first and then by id; empty if the main one has none.
std::vector<RoomsOfAGrid> GridsWithRooms(const GridRoomsQuery& query, flecs::world world) {
  // Components are added first: a grid changing table would move the caches the result refers to.
  std::vector<flecs::entity> unbased;
  query.each([&unbased](flecs::entity entity, const Grid&, const RoomCache& cache) {
    if (cache.Current() && !entity.has<RoomAirBasis>()) {
      unbased.push_back(entity);
    }
  });
  if (!unbased.empty()) {
    const z13::ImmediateScope immediate(world);
    std::ranges::for_each(unbased, [](flecs::entity entity) { entity.set(RoomAirBasis {}); });
  }
  std::vector<RoomsOfAGrid> found;
  query.each([&found](flecs::entity entity, const Grid& grid, const RoomCache& cache) {
    if (cache.Current()) {
      found.push_back({.entity = entity, .id = grid.id, .cache = cache, .graph = *cache.Current()});
    }
  });
  std::ranges::sort(found, {}, &RoomsOfAGrid::id);
  if (found.empty() || found.front().id != kMainGrid) {
    return {};
  }
  return found;
}

RoomAirBasis& BasisOf(flecs::entity grid) {
  return grid.get_mut<RoomAirBasis>();
}

RoomsNow RoomsNowOf(const std::vector<RoomsOfAGrid>& grids, const AtmosQueries& queries,
                    const BlockPalette& palette, DoorOpeningsCache& door_cache, flecs::world& world) {
  std::erase_if(door_cache, [&world](const auto& entry) { return !world.is_alive(entry.first); });
  RoomsNow now;
  size_t first = 0;
  for (const RoomsOfAGrid& rooms : grids) {
    GridAir grid {.grid = rooms.id, .graph = rooms.graph, .index = IndexOf(rooms.graph), .first = first};
    grid.doors = DoorsOf(queries.doors, palette, rooms.id, door_cache);
    if (rooms.id != kMainGrid) {
      grid.from_world = GridToWorld(rooms.entity.get<Grid>()).inverse();
    }
    first += grid.index.ids.size();
    now.grids.push_back(std::move(grid));
  }
  const GridAir& main = now.Main();
  now.airlocks = FindAirlocks(queries.airlocks, world, main.graph, main.index, main.doors);
  return now;
}

void Update(flecs::iter& it, const AtmosQueries& queries, DoorOpeningsCache& door_cache, const StationNetworks& stations, const BlockPalette& palette,
            const z13::AtmosTuning& tuning, const z13::DoorTuning& door_tuning,
            const z13::station::Interactions& interactions, const DockLinks& docks,
            const z13::flecs_tools::SimulationClock& clock) {
  flecs::world world = it.world();
  const std::vector<RoomsOfAGrid> grids = GridsWithRooms(queries.grids, world);
  if (grids.empty()) {
    return;
  }
  size_t room_count = 0;
  bool follows = true;
  for (const RoomsOfAGrid& grid : grids) {
    const RoomAirBasis& basis = BasisOf(grid.entity);
    follows = follows && basis.seeded && basis.fingerprint == grid.cache.get().CurrentFingerprint();
    room_count += grid.graph.get().ByAnchor().size();
  }
  follows = follows && static_cast<size_t>(queries.stored.count()) == room_count;
  const bool step = BasisOf(grids.front().entity).seeded && clock.tick % tuning.step_ticks == 0;
  RoomsNow now;
  if (!follows || step) {
    now = RoomsNowOf(grids, queries, palette, door_cache, world);
  }
  // Every tick, so that a use of an airlock's door is never missed between two steps of the air.
  const std::unordered_set<flecs::entity_t> held =
      TendAirlocks(queries.airlocks, queries.interlocks, interactions, world);
  if (follows && !step) {
    return;
  }
  const std::vector<RoomAirEntry> stored = LoadRoomAir(queries.stored);
  std::vector<RoomAirEntry> rooms;
  std::vector<z13::station::GridId> ids;
  for (const RoomsOfAGrid& grid : grids) {
    std::vector<RoomAirEntry> of_grid = RoomsOfGrid(stored, grid.id);
    if (!follows) {
      FollowRooms(BasisOf(grid.entity), of_grid, grid.cache, grid.graph);
      std::ranges::for_each(of_grid, [&grid](RoomAirEntry& room) { room.grid = grid.id; });
    }
    std::ranges::move(of_grid, std::back_inserter(rooms));
    ids.push_back(grid.id);
  }
  if (BasisOf(grids.front().entity).seeded && step) {
    Step(rooms, now, held, stations, palette, tuning, door_tuning, docks, world, queries.players);
  }
  StoreRoomAir(world, queries.stored, rooms, ids);
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
                              .grids = world.query<const Grid, const RoomCache>(),
                              .airlocks = world.query<const AirlockCycle, const Block>(),
                              .interlocks = world.query<const DoorInterlock>()};
  const auto reader = std::make_shared<BlockChangeReader>(world);
  const auto door_cache = std::make_shared<DoorOpeningsCache>();
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
              const z13::station::Interactions, const DockLinks, const z13::flecs_tools::SimulationClock>(
                  "AtmosSystem::Update")
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
      .each([queries, door_cache](flecs::iter& it, size_t, const StationNetworks& stations, const BlockPalette& palette,
                      const z13::AtmosTuning& tuning, const z13::DoorTuning& door_tuning,
                      const z13::station::Interactions& interactions, const DockLinks& docks,
                      const z13::flecs_tools::SimulationClock& clock) {
        Update(it, queries, *door_cache, stations, palette, tuning, door_tuning, interactions, docks, clock);
      });
}

}  // namespace

void AtmosSystem::Register(flecs::world& world) {
  z13::OnRegisterComponents(world, RegisterComponents);
  z13::OnInitPhases(world, RegisterPipeline);
  z13::OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::atmos_module
