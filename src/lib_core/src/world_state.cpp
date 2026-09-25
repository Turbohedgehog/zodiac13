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

#include <lib_core/world_state.h>

#include <Eigen/Dense>

#include <lib_core/rollback.h>
#include <lib_core/simulation_clock.h>
#include <lib_core/world_snapshot_history.h>
#include <lib_core/world_state_requests.h>

namespace z13::flecs_tools {

bool IsStateSingleton(flecs::entity component) {
  return component.has<flecs::Component>() && component.has<StateComponent>() &&
         component.has(flecs::Singleton);
}

void RegisterStateMeta(flecs::world& world) {
  world.component<StateEntity>();
  world.component<StateComponent>();
  RegisterStdStringMeta(world);
  RegisterEigenMeta(world);
  // Shared transform type; only state entities are captured, so this is safe module-wide.
  world.component<Eigen::Matrix4f>().add<StateComponent>();
  RegisterWorldStateRequests(world);
  RegisterSimulationClock(world);
  RegisterWorldSnapshotHistory(world);
  RegisterRollback(world);
}

}  // namespace z13::flecs_tools
