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

#include <optional>
#include <utility>

#include <Eigen/Dense>

#include <lib_core/state/world_state.h>
#include <lib_core/utils/math.h>
#include <lib_core/world/lifecycle.h>

#include <z13/components/gravity.h>
#include <z13/components/rooms.h>
#include <z13/components/station.h>
#include <rooms/room_cache.h>
#include <z13_settings/physics_tuning.h>

#include "room_gravity.h"

namespace z13::station {

namespace {

using z13::gravity::Gravity;
using z13::station::rooms::RoomCache;

void RegisterComponents(flecs::world world) {
  z13::flecs_tools::RegisterComponents<RoomGravity>(world);
}

void RegisterPipeline(flecs::world world) {
  world.component<GravityPhase>().add(flecs::Phase).depends_on<StationTopologyPhase>();
}

// Without rooms (a failed rebuild) RoomGravity is empty and nothing pulls.
void UpdateRoomGravity(RoomGravity& gravity, const RoomCache& cache, const z13::PhysicsTuning& tuning) {
  const auto graph = cache.Current();
  if (!graph) {
    gravity = {};
    return;
  }
  const RoomGravitySource source {.fingerprint = *cache.CurrentFingerprint(), .gravity = tuning.gravity};
  if (gravity.source == source) {
    return;
  }
  RoomGravity updated {.source = source};
  updated.by_room.assign(graph->get().rooms.size(), Eigen::Vector3f(0.f, 0.f, -tuning.gravity));
  updated.by_room[rooms::kVacuumRoom] = Eigen::Vector3f::Zero();
  gravity = std::move(updated);
}

void SetAcceleration(Gravity& gravity, const Eigen::Vector3f& acceleration) {
  if (gravity.acceleration != acceleration) {
    gravity = {.acceleration = acceleration};
  }
}

// A body inside a block (a door frame, a wall it's pushed out of) keeps what it had, so
// passing a door doesn't drop gravity for a few ticks.
void ApplyRoomGravity(flecs::iter& it) {
  while (it.next()) {
    const auto transforms = it.field<const Eigen::Matrix4f>(0);
    const auto bodies = it.field<Gravity>(1);
    const RoomGravity& rooms_gravity = it.field<const RoomGravity>(2)[0];
    const auto graph = it.field<const RoomCache>(3)[0].Current();
    for (const size_t i : it) {
      if (!graph || !rooms_gravity.source) {
        SetAcceleration(bodies[i], Eigen::Vector3f::Zero());
        continue;
      }
      const Eigen::Vector3f cell = z13::math::ExtractTranslation<float>(transforms[i]) / kCellSize;
      if (const std::optional<rooms::RoomIndex> room = graph->get().RoomAt(cell.array().floor().cast<int>())) {
        SetAcceleration(bodies[i], rooms_gravity.by_room[*room]);
      }
    }
  }
}

void RegisterSystems(flecs::world world) {
  world.set(RoomGravity {});

  world.system<RoomGravity, const RoomCache, const z13::PhysicsTuning>("GravitySystem::UpdateRoomGravity")
      .kind<GravityPhase>()
      .with<StationMode>()
      .each(UpdateRoomGravity);

  world.system<const Eigen::Matrix4f, Gravity, const RoomGravity, const RoomCache>(
           "GravitySystem::ApplyRoomGravity")
      .kind<GravityPhase>()
      .with<StationMode>()
      .run(ApplyRoomGravity);
}

}  // namespace

void GravitySystem::Register(flecs::world& world) {
  OnRegisterComponents(world, RegisterComponents);

  OnInitPhases(world, RegisterPipeline);

  OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::station
