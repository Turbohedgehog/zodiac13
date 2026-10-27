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

#include "gameplay_system.h"

#include <flecs.h>

#include <lib_core/state/state_markers.h>
#include <lib_core/time/simulation_clock.h>
#include <lib_core/utils/flecs_utils.h>
#include <lib_core/utils/log.h>
#include <lib_core/world/components.h>
#include <lib_core/world/lifecycle.h>

#include <z13/components/gameplay.h>
#include <z13/components/gameplay_phases.h>
#include <z13/components/player.h>
#include <z13/components/net.h>

#include <z13_module/gameplay/gameplay_entities.h>


namespace z13::gameplay {

namespace {

void RegisterPipeline(flecs::world world) {
  world.component<PreUpdatePhase>().add(flecs::Phase).depends_on(flecs::OnUpdate);
  world.component<UpdatePhase>().add(flecs::Phase).depends_on<PreUpdatePhase>();
  world.component<PostUpdatePhase>().add(flecs::Phase).depends_on<UpdatePhase>();
  world.get_alive(flecs::OnValidate).add(flecs::Phase).depends_on<PostUpdatePhase>();
}

void OnInit(flecs::iter it, size_t /*i*/, const gameplay::Gameplay&) {
  flecs::world world = it.world();
  if (world.has<z13::net::ClientRole>()) {
    // A client's scene/IdCounters/LocalPlayer.id all come from Welcome/Resync's snapshot,
    // applied before Gameplay was added -- nothing to spawn here.
    return;
  }

  // Immediate, so SpawnPlayer below sees what the observers add; they name it by these counters.
  {
    const z13::ImmediateScope immediate(world);
    world.set<gameplay::IdCounters>({});
    const flecs::entity target = world.entity().add<PopulateSceneEvent>();
    world.event<PopulateSceneEvent>().id<PopulateSceneEvent>().entity(target).emit();
    target.destruct();
  }

  // Single-player and the server both play as id 0.
  gameplay::IdCounters counters = world.get<gameplay::IdCounters>();
  const uint32_t local_id = counters.last_player_id++;
  SpawnPlayer(world, local_id);
  world.set<gameplay::IdCounters>(counters);
  world.set<gameplay::LocalPlayer>({.id = local_id});
  // Callers expect the local player ready right after this observer runs, before any
  // progress() -- the per-frame re-derivation in gameplay_input_system.cpp is too late.
  EnsureLocalPlayerReady(world);
}

// Only single-player pauses; runs in PreFrame so a frozen world can thaw. A joining client has no
// scene to simulate: its clock would run past the server's snapshot before it arrives.
void SyncSimulationFrozen(flecs::iter& it) {
  flecs::world world = it.world();
  const bool client = world.has<z13::net::ClientRole>();
  const bool paused = world.has<Pause>() && !world.has<z13::net::ServerRole>() && !client;
  const bool frozen = paused || (client && !world.has<gameplay::Gameplay>());
  if (frozen == world.has<z13::flecs_tools::SimulationFrozen>()) {
    return;
  }
  log_debug("Gameplay: simulation {}", frozen ? "frozen" : "running");
  if (frozen) {
    world.add<z13::flecs_tools::SimulationFrozen>();
  } else {
    world.remove<z13::flecs_tools::SimulationFrozen>();
  }
}

void OnTeardown(flecs::iter it, size_t /*i*/, const gameplay::Gameplay&) {
  it.world().query_builder().with<z13::flecs_tools::StateEntity>().build()
    .each([](flecs::entity e) { e.destruct(); });
}

void RegisterSystems(flecs::world world) {
  world.observer<gameplay::Gameplay>("GameplaySystem::OnInit")
    .event(flecs::OnAdd)
    .yield_existing()
    .each(OnInit);

  // Gameplay marks "a scene exists"; removing it (exit to main menu) destroys the scene.
  world.observer<gameplay::Gameplay>("GameplaySystem::OnTeardown")
    .event(flecs::OnRemove)
    .each(OnTeardown);

  world.observer<const WindowFocusEvent>("WindowFocusEvent::OnSet")
    .event(flecs::OnSet)
    .yield_existing()
    .each([](flecs::iter& it, size_t, const WindowFocusEvent& window_focus_event) {
      log_debug("Gameplay: window focus {}", window_focus_event.has_focus ? "gained" : "lost");
      if (!window_focus_event.has_focus) {
        it.world().add<Pause>();
      }
    });

  world.observer("GameplaySystem::LogPause")
    .with<Pause>()
    .event(flecs::OnAdd)
    .event(flecs::OnRemove)
    .each([](flecs::iter& it, size_t) {
      log_debug("Gameplay: pause {}", it.event() == flecs::OnAdd ? "on" : "off");
    });

  world.system("GameplaySystem::SyncSimulationFrozen")
    .kind(flecs::PreFrame)
    .read<Pause>()
    .read<z13::net::ServerRole>()
    .read<z13::net::ClientRole>()
    .read<gameplay::Gameplay>()
    .write<z13::flecs_tools::SimulationFrozen>()
    .run(SyncSimulationFrozen);
}

}  // namespace

void GameplaySystem::Register(flecs::world& world) {
  OnInitPhases(world, RegisterPipeline);

  OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::gameplay
