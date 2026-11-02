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

#include "gravity_system.h"

#include <algorithm>
#include <optional>
#include <vector>
#include <utility>

#include <Eigen/Dense>

#include <lib_core/state/world_state.h>
#include <lib_core/utils/math.h>
#include <lib_core/world/lifecycle.h>

#include <z13/components/utility/gravity.h>
#include <z13/components/station/grid.h>
#include <z13/components/station/grid_pose.h>
#include <z13/components/station/rooms.h>
#include <z13/components/station/block.h>
#include <z13/components/station/station_mode.h>
#include <z13/components/utility/utilities.h>
#include <rooms/room_cache.h>
#include <z13_settings/physics_tuning.h>

#include "room_gravity.h"

namespace z13::gravity_module {

namespace {

using z13::gravity::Gravity;
using z13::gravity::GravitySources;
using z13::station::Grid;
using z13::station::kMainGrid;
using z13::station::kCellSize;
using z13::station::MainGridData;
using z13::station::StationMode;
using z13::station::StationTopologyPhase;
namespace rooms = z13::station::rooms;
using rooms::RoomCache;

void RegisterComponents(flecs::world world) {
  z13::flecs_tools::RegisterComponents<RoomGravity, GravitySources>(world);
}

void RegisterPipeline(flecs::world world) {
  // The generators' charge is set by the consumers of the tick, so a rolled-back tick reads its own.
  world.component<GravityPhase>()
      .add(flecs::Phase)
      .depends_on<StationTopologyPhase>()
      .depends_on<z13::utility::UtilityConsumersPhase>();
}

// The share of the pull a room feels: all of it where the station has no generator, otherwise that of the strongest
// one whose block lies within `radius_cells` of the room's box.
float ShareOf(const rooms::Room& room, const GravitySources& sources, uint32_t radius_cells) {
  if (sources.list.empty()) {
    return 1.f;
  }
  const auto radius = static_cast<int64_t>(radius_cells);
  const Eigen::Vector3i last = room.bounds.End() - Eigen::Vector3i::Ones();
  float share = 0.f;
  for (const z13::gravity::GravitySource& source : sources.list) {
    const Eigen::Vector3i nearest = source.cell.cwiseMax(room.bounds.min).cwiseMin(last);
    if ((nearest - source.cell).cast<int64_t>().squaredNorm() <= radius * radius) {
      share = std::max(share, static_cast<float>(source.level) / static_cast<float>(z13::gravity::kGravityFull));
    }
  }
  return share;
}

Eigen::Vector3f RoomPull(const rooms::RoomGraph& graph, rooms::RoomId room, const GravitySources& sources,
                         const z13::PhysicsTuning& tuning) {
  const auto found = graph.rooms.find(room);
  if (room == rooms::kVacuumRoom || found == graph.rooms.end()) {
    return Eigen::Vector3f::Zero();
  }
  return {0.f, 0.f, -tuning.gravity * ShareOf(found->second, sources, tuning.generator_radius_cells)};
}

// Without rooms (a failed rebuild) RoomGravity is empty and nothing pulls.
void UpdateRoomGravity(flecs::iter& it, size_t, RoomGravity& gravity, const z13::PhysicsTuning& tuning,
                       const GravitySources& sources) {
  const auto main_cache = MainGridData<RoomCache>(it.world());
  const auto graph = main_cache ? main_cache->get().Current() : std::nullopt;
  if (!graph) {
    gravity = {};
    return;
  }
  const RoomCache& cache = main_cache->get();
  const RoomGravitySource source {.switches = cache.Switches(),
                                  .gravity = tuning.gravity,
                                  .generator_radius_cells = tuning.generator_radius_cells,
                                  .sources = sources.list};
  if (gravity.source == source) {
    return;
  }
  const bool same_pull = gravity.source && gravity.source->gravity == source.gravity &&
                         gravity.source->generator_radius_cells == source.generator_radius_cells &&
                         gravity.source->sources == source.sources;
  const auto changes = same_pull ? cache.ChangesSince(gravity.source->switches) : std::nullopt;
  if (changes) {
    std::ranges::for_each(changes->removed, [&gravity](rooms::RoomId room) { gravity.by_room.erase(room); });
    for (const rooms::RoomId room : changes->added) {
      gravity.by_room.emplace(room, RoomPull(graph->get(), room, sources, tuning));
    }
    gravity.source = source;
    return;
  }
  RoomGravity updated {.source = source};
  for (const auto& [id, room] : graph->get().rooms) {
    updated.by_room.emplace(id, RoomPull(graph->get(), id, sources, tuning));
  }
  gravity = std::move(updated);
}

void SetAcceleration(Gravity& gravity, const Eigen::Vector3f& acceleration) {
  if (gravity.acceleration != acceleration) {
    gravity = {.acceleration = acceleration};
  }
}

using GridQuery = flecs::query<const Grid>;

// A grid other than the main one with its rooms, and the pose that takes the world's meters into its own.
struct DistrictRooms {
  Eigen::Isometry3f from_world;
  // Its down in the world: the grid's own -Z turned by its pose.
  Eigen::Vector3f down;
  rooms::OptionalRoomGraph graph;
};

std::vector<DistrictRooms> DistrictsOf(const GridQuery& grids) {
  std::vector<DistrictRooms> districts;
  grids.each([&districts](flecs::entity entity, const Grid& grid) {
    const auto* cache = entity.try_get<RoomCache>();
    if (grid.id == kMainGrid || cache == nullptr || !cache->Current()) {
      return;
    }
    const Eigen::Isometry3f to_world = GridToWorld(grid);
    districts.push_back({.from_world = to_world.inverse(),
                         .down = to_world.linear() * -Eigen::Vector3f::UnitZ(),
                         .graph = cache->Current()});
  });
  return districts;
}

// What a body at `position` (meters) feels from the rooms of the districts: the pull of the first room that has it, or
// that it is inside a block of one.
struct DistrictPull {
  std::optional<Eigen::Vector3f> pull;
  bool inside_block {};
};

DistrictPull FeelDistricts(const std::vector<DistrictRooms>& districts, const Eigen::Vector3f& position, float gravity) {
  DistrictPull felt;
  for (const DistrictRooms& district : districts) {
    const Eigen::Vector3f cell = (district.from_world * position) / kCellSize;
    const std::optional<rooms::RoomId> room = district.graph->get().RoomAt(cell.array().floor().cast<int>());
    if (!room) {
      felt.inside_block = true;
    } else if (*room != rooms::kVacuumRoom) {
      felt.pull = district.down * gravity;
      return felt;
    }
  }
  return felt;
}

// A body inside a block (a door frame, a wall it's pushed out of) keeps what it had, so
// passing a door doesn't drop gravity for a few ticks. The rooms of the main grid come first, then those of the other
// grids by their poses: a district's gravity is its own down.
void ApplyRoomGravity(flecs::iter& it, const GridQuery& grids) {
  const auto main_cache = MainGridData<RoomCache>(it.world());
  const auto graph = main_cache ? main_cache->get().Current() : std::nullopt;
  const std::vector<DistrictRooms> districts = DistrictsOf(grids);
  while (it.next()) {
    const auto transforms = it.field<const Eigen::Matrix4f>(0);
    const auto bodies = it.field<Gravity>(1);
    const RoomGravity& rooms_gravity = it.field<const RoomGravity>(2)[0];
    const float gravity = it.field<const z13::PhysicsTuning>(3)[0].gravity;
    const bool main_known = graph && rooms_gravity.source;
    for (const size_t i : it) {
      const Eigen::Vector3f position = z13::math::ExtractTranslation<float>(transforms[i]);
      std::optional<rooms::RoomId> room;
      if (main_known) {
        room = graph->get().RoomAt((position / kCellSize).array().floor().cast<int>());
      }
      if (room && *room != rooms::kVacuumRoom) {
        if (const auto pull = rooms_gravity.by_room.find(*room); pull != rooms_gravity.by_room.end()) {
          SetAcceleration(bodies[i], pull->second);
        }
        continue;
      }
      const DistrictPull district = FeelDistricts(districts, position, gravity);
      if (district.pull) {
        SetAcceleration(bodies[i], *district.pull);
      } else if (!main_known) {
        SetAcceleration(bodies[i], Eigen::Vector3f::Zero());
      } else if (!district.inside_block && room) {
        SetAcceleration(bodies[i], Eigen::Vector3f::Zero());
      }
    }
  }
}

void RegisterSystems(flecs::world world) {
  world.set(RoomGravity {});

  world.set(GravitySources {});

  world.system<RoomGravity, const z13::PhysicsTuning, const GravitySources>("GravitySystem::UpdateRoomGravity")
      .kind<GravityPhase>()
      .with<StationMode>()
      .read<RoomCache>()
      .each(UpdateRoomGravity);

  const GridQuery grids = world.query<const Grid>();
  world.system<const Eigen::Matrix4f, Gravity, const RoomGravity, const z13::PhysicsTuning>(
           "GravitySystem::ApplyRoomGravity")
      .kind<GravityPhase>()
      .with<StationMode>()
      .read<RoomCache>()
      .read<Grid>()
      .run([grids](flecs::iter& it) { ApplyRoomGravity(it, grids); });
}

}  // namespace

void GravitySystem::Register(flecs::world& world) {
  OnRegisterComponents(world, RegisterComponents);

  OnInitPhases(world, RegisterPipeline);

  OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::gravity_module
