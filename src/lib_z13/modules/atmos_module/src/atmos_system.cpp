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
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <unordered_map>
#include <vector>

#include <flecs.h>

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
#include <z13/components/door.h>
#include <z13/components/grid.h>
#include <z13/components/player.h>
#include <z13/components/station_mode.h>
#include <z13/components/utilities.h>
#include <z13_settings/atmos_tuning.h>

#include "room_air_carry.h"
#include "room_air_store.h"

namespace z13::atmos_module {

namespace {

using z13::building::primitives::BlockPalette;
using z13::building::primitives::CellBox;
using z13::station::Block;
using z13::station::DoorState;
using z13::station::Grid;
using z13::station::kCellSize;
using z13::station::kMainGrid;
using z13::station::rooms::Portal;
using z13::station::rooms::RoomCache;
using z13::station::rooms::RoomGraph;
using z13::station::rooms::RoomId;
using z13::utility::DoorInterlock;
using z13::utility::RoomAirBasis;
using z13::utility::RoomAirEntry;
using z13::utility::networks::StationNetworks;
namespace atmos = z13::utility::atmos;

using DoorQuery = flecs::query<const DoorState, const Block>;
using PlayerQuery = flecs::query<const z13::gameplay::Player, const Eigen::Matrix4f>;

// The chambers are the rooms in anchor order.
struct RoomIndex {
  std::vector<RoomId> ids;
  std::unordered_map<RoomId, size_t> chamber_of;
};

RoomIndex IndexOf(const RoomGraph& graph) {
  RoomIndex index {.ids = graph.ByAnchor()};
  for (size_t i = 0; i < index.ids.size(); ++i) {
    index.chamber_of.emplace(index.ids[i], i);
  }
  return index;
}

size_t ChamberOf(const RoomIndex& index, RoomId room) {
  const auto found = index.chamber_of.find(room);
  return found != index.chamber_of.end() ? found->second : atmos::kOutside;
}

struct DoorAt {
  flecs::entity entity;
  std::vector<CellBox> openings;
  z13::station::DoorPosition position {};
};

std::vector<DoorAt> DoorsOf(const DoorQuery& doors, const BlockPalette& palette) {
  std::vector<DoorAt> found;
  doors.each([&](flecs::entity entity, const DoorState& state, const Block& block) {
    const auto primitive = block.grid == kMainGrid ? palette.palette.Find(block.spec.type_id) : std::nullopt;
    if (!primitive) {
      return;
    }
    DoorAt door {.entity = entity, .position = state.position};
    for (const auto& source : z13::station::rooms::PortalsOf(block, primitive->get(), palette.palette)) {
      door.openings.push_back(source.opening);
    }
    found.push_back(std::move(door));
  });
  return found;
}

bool Overlaps(const DoorAt& door, const CellBox& opening) {
  return std::ranges::any_of(door.openings, [&opening](const CellBox& at) { return at.Overlaps(opening); });
}

// A door lets through the share of the portal's area that it is open; a doorway without a door all of it; a window
// or a wall nothing.
int64_t OpenArea(const Portal& portal, std::span<const DoorAt> doors) {
  const auto door = std::ranges::find_if(doors, [&portal](const DoorAt& at) { return Overlaps(at, portal.opening); });
  if (door != doors.end()) {
    return portal.area_cells * static_cast<int64_t>(door->position) / z13::station::kDoorOpen;
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

// The rooms touching the faces of a block, which fills cells of its own: a vent lets its network into them.
std::vector<size_t> ChambersAround(const RoomGraph& graph, const RoomIndex& index, const CellBox& block) {
  std::vector<size_t> chambers;
  const CellBox around = block.Grown(Eigen::Vector3i::Ones());
  for (int x = around.min.x(); x < around.End().x(); ++x) {
    for (int y = around.min.y(); y < around.End().y(); ++y) {
      for (int z = around.min.z(); z < around.End().z(); ++z) {
        const Eigen::Vector3i cell(x, y, z);
        const Eigen::Index outside =
            ((cell.array() < block.min.array()) || (cell.array() >= block.End().array())).count();
        if (outside != 1) {
          continue;
        }
        const auto room = graph.RoomAt(cell);
        const size_t chamber = room ? ChamberOf(index, *room) : atmos::kOutside;
        if (chamber != atmos::kOutside) {
          chambers.push_back(chamber);
        }
      }
    }
  }
  return chambers;
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
    for (const size_t chamber : vented) {
      atmos::Regulate(chambers[chamber].air, chambers[chamber].volume_cells,
                      Shared(unit, static_cast<int64_t>(vented.size())));
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

int64_t PressureOf(const std::vector<atmos::Chamber>& chambers, size_t chamber) {
  return chamber == atmos::kOutside ? 0 : atmos::PressurePascals(chambers[chamber].air, chambers[chamber].volume_cells);
}

int64_t DifferenceAt(const DoorAt& door, const RoomGraph& graph, const RoomIndex& index,
                     const std::vector<atmos::Chamber>& chambers) {
  int64_t greatest = 0;
  for (const Portal& portal : graph.portals) {
    if (Overlaps(door, portal.opening)) {
      const int64_t difference =
          PressureOf(chambers, ChamberOf(index, portal.a)) - PressureOf(chambers, ChamberOf(index, portal.b));
      greatest = std::max(greatest, std::abs(difference));
    }
  }
  return greatest;
}

// Locked while the difference is great: an automatic door does not open into a room that would empty through it.
void LockDoors(std::span<const DoorAt> doors, const RoomGraph& graph, const RoomIndex& index,
               const std::vector<atmos::Chamber>& chambers, const z13::AtmosTuning& tuning) {
  for (const DoorAt& door : doors) {
    const bool locked = DifferenceAt(door, graph, index, chambers) > tuning.interlock_pascals;
    if (locked && !door.entity.has<DoorInterlock>()) {
      door.entity.set(DoorInterlock {.reason = z13::utility::InterlockReason::kPressure});
    } else if (!locked && door.entity.has<DoorInterlock>()) {
      door.entity.remove<DoorInterlock>();
    }
  }
}

void FollowRooms(RoomAirBasis& basis, std::vector<RoomAirEntry>& rooms, const RoomCache& cache, const RoomGraph& graph) {
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

void Step(std::vector<RoomAirEntry>& rooms, const RoomGraph& graph, const StationNetworks& stations, const BlockPalette& palette,
          const z13::AtmosTuning& tuning, const flecs::world& world, const DoorQuery& door_query,
          const PlayerQuery& players) {
  const RoomIndex index = IndexOf(graph);
  const std::vector<DoorAt> doors = DoorsOf(door_query, palette);
  std::vector<atmos::Chamber> chambers = ChambersOf(rooms, graph, index);

  const std::vector<atmos::Link> links = LinksOf(graph, index, doors);
  atmos::Exchange(chambers, links, {.coefficient = tuning.flow_coefficient, .max_outflow_ppm = tuning.max_outflow_ppm});
  BreatheRooms(chambers, graph, index, players, tuning);
  RegulateRooms(stations, world, palette, graph, index, chambers);
  LockDoors(doors, graph, index, chambers, tuning);

  for (size_t i = 0; i < chambers.size(); ++i) {
    rooms[i] = EntryOf(rooms[i].anchor, chambers[i].air);
  }
}

void RegisterComponents(flecs::world world) {
  z13::flecs_tools::RegisterComponents<RoomAirBasis, RoomAirEntry, DoorInterlock>(world);
}

void Update(flecs::iter& it, const DoorQuery& doors, const PlayerQuery& players, const RoomAirQuery& stored,
            const StationNetworks& stations, const BlockPalette& palette, const z13::AtmosTuning& tuning,
            const z13::flecs_tools::SimulationClock& clock) {
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
                       static_cast<size_t>(stored.count()) == graph->get().ByAnchor().size();
  const bool step = basis.seeded && clock.tick % tuning.step_ticks == 0;
  if (follows && !step) {
    return;
  }
  std::vector<RoomAirEntry> rooms = LoadRoomAir(stored);
  if (!follows) {
    FollowRooms(basis, rooms, cache->get(), graph->get());
  }
  if (basis.seeded && step) {
    Step(rooms, graph->get(), stations, palette, tuning, world, doors, players);
  }
  StoreRoomAir(world, stored, rooms);
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

  const DoorQuery doors = world.query<const DoorState, const Block>();
  const PlayerQuery players = world.query<const z13::gameplay::Player, const Eigen::Matrix4f>();
  const RoomAirQuery stored = world.query<const RoomAirEntry>();
  world.system<const StationNetworks, const BlockPalette, const z13::AtmosTuning, const z13::flecs_tools::SimulationClock>(
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
      .each([doors, players, stored](flecs::iter& it, size_t, const StationNetworks& stations, const BlockPalette& palette,
                                     const z13::AtmosTuning& tuning, const z13::flecs_tools::SimulationClock& clock) {
        Update(it, doors, players, stored, stations, palette, tuning, clock);
      });
}

}  // namespace

void AtmosSystem::Register(flecs::world& world) {
  z13::OnRegisterComponents(world, RegisterComponents);
  z13::OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::atmos_module
