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

#include <lib_core/log.h>
#include <lib_core/components.h>
#include <lib_core/world_state.h>

#include <z13/components/gameplay.h>
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

void UpdateGameplay() {
  // log_info("UpdateGameplay()");
}

void OnGameplay() {
  // log_info("Gameplay()");
}

void ValidateGameplay() {
  // log_info("ValidateGameplay()");
}

void OnInit(flecs::iter it, size_t /*i*/, const gameplay::Gameplay&) {
  const flecs::world world = it.world();
  if (world.has<z13::net::ClientRole>()) {
    // A client's scene/IdCounters/LocalPlayer.id all come from Welcome/Resync's snapshot,
    // applied before Gameplay was added -- nothing to spawn here.
    log_info("~~~~ gameplay::OnInit (client)");
    return;
  }

  // Single-player and the server both play as id 0.
  gameplay::IdCounters counters;
  const uint32_t local_id = counters.last_player_id++;
  SpawnPlayer(world, local_id);
  world.set<gameplay::IdCounters>(counters);
  world.set<gameplay::LocalPlayer>({.id = local_id});
  // Callers expect the local player ready right after this observer runs, before any
  // progress() -- the per-frame re-derivation in gameplay_input_system.cpp is too late.
  EnsureLocalPlayerReady(world);

  log_info("~~~~ gameplay::OnInit");
}

void OnTeardown(flecs::iter it, size_t /*i*/, const gameplay::Gameplay&) {
  it.world().query_builder().with<z13::flecs_tools::StateEntity>().build()
    .each([](flecs::entity e) { e.destruct(); });
}

void RegisterSystems(flecs::world world) {
  world.observer<gameplay::Gameplay>("GameplaySystem::OnInit")
    .event(flecs::OnAdd)
    .yield_existing()
    // .each([world](const auto& gameplay) { OnInit(world, gameplay); });
    .each(OnInit);

  // Gameplay marks "a scene exists"; removing it (exit to main menu) destroys the scene.
  world.observer<gameplay::Gameplay>("GameplaySystem::OnTeardown")
    .event(flecs::OnRemove)
    .each(OnTeardown);

  world.observer<const WindowFocusEvent>("WindowFocusEvent::OnSet")
    .event(flecs::OnSet)
    .yield_existing()
    .each([world](const auto& window_focus_event) {
      if (!window_focus_event.has_focus) {
        world.add<Pause>();
      }
    });

  world.system("UpdateGameplaySystem")
    .kind<UpdatePhase>()
    .each(UpdateGameplay);

  world.system("OnGameplaySystem")
    .kind<UpdatePhase>()
    .each(OnGameplay);

  world.system("ValidateGameplaySystem")
    .kind(flecs::OnValidate)
    .each(ValidateGameplay);
}

}  // namespace

void GameplaySystem::Register(flecs::world& world) {
  world.observer<InitPhasesEvent>("GameplaySystem::RegisterPipeline")
    .event(flecs::OnSet)
    .yield_existing()
    .each([world = world](const auto&) {
      RegisterPipeline(world);
    });

  world.observer<InitSystemsEvent>("GameplaySystem::RegisterSystems")
    .event(flecs::OnSet)
    .yield_existing()
    .each([world = world](const auto&) {
      RegisterSystems(world);
    });
}

}  // namespace z13::gameplay
