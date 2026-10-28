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


#include "block_change_system.h"

#include <flecs.h>

#include <lib_core/state/world_state.h>
#include <lib_core/world/lifecycle.h>

#include <z13/components/block_changes.h>
#include <z13/components/gameplay_phases.h>
#include <z13/components/state_changes.h>

namespace z13::gameplay {

namespace {

// The index and the bodies read the changes by PostUpdatePhase.
void RegisterPipeline(flecs::world world) {
  world.component<z13::station::ClearBlockChangesPhase>().add(flecs::Phase).depends_on<PostUpdatePhase>();
}

void RegisterComponents(flecs::world world) {
  z13::flecs_tools::RegisterComponents<z13::gameplay::EditActor>(world);
}

void RegisterSystems(flecs::world world) {
  world.set(z13::gameplay::EditActor {});
  world.system("BlockChangeSystem::ClearStateChanges")
      .kind<z13::station::ClearBlockChangesPhase>()
      .with<z13::gameplay::StateChange>()
      .write<z13::gameplay::StateChange>()
      .each([](flecs::entity change) { change.destruct(); });
  world.system("BlockChangeSystem::ClearChanges")
      .kind<z13::station::ClearBlockChangesPhase>()
      .with<z13::station::BlockChange>()
      .write<z13::station::BlockChange>()
      .each([](flecs::entity change) { change.destruct(); });
}

}  // namespace

void BlockChangeSystem::Register(flecs::world& world) {
  OnInitPhases(world, RegisterPipeline);
  OnRegisterComponents(world, RegisterComponents);

  OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::gameplay
