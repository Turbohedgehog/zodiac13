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

#include "scene_reload_system.h"

#include <Eigen/Dense>

#include <lib_core/state/world_state.h>
#include <lib_core/world/lifecycle.h>
#include <player/gameplay_entities.h>
#include <z13/components/gameplay/gameplay.h>
#include <z13/components/gameplay/player.h>

#include "scene_reload.h"

namespace z13::station_editor {

namespace {

void Restart(flecs::world world, SceneReload& reload) {
  const bool running = world.has<z13::gameplay::Gameplay>();
  if (reload.pending && !running) {
    world.add<z13::gameplay::Gameplay>();
    reload.pending = false;
    return;
  }
  const auto* local = world.try_get<z13::gameplay::LocalPlayer>();
  if (!reload.pose || !running || local == nullptr || !local->id) {
    return;
  }
  // The frame after the restart: the commands that add Gameplay are deferred, so the player exists only now.
  const flecs::entity player = world.lookup(z13::gameplay::PlayerEntityName(*local->id).c_str());
  if (player.is_valid()) {
    player.set<Eigen::Matrix4f>(*reload.pose);
    reload.pose.reset();
  }
}

}  // namespace

void SceneReloadSystem::Register(flecs::world& world) {
  z13::OnRegisterComponents(world, [](flecs::world& w) { flecs_tools::RegisterComponent<SceneReload>(w); });
  z13::OnInitSystems(world, [](flecs::world& w) {
    w.set(SceneReload {});
    w.system<SceneReload>("SceneReloadSystem::Restart")
        .kind(flecs::PreFrame)  // a frame phase: the simulation phases are off while Gameplay is gone
        .immediate()
        .each([](flecs::iter& it, size_t, SceneReload& reload) { Restart(it.world(), reload); });
  });
}

}  // namespace z13::station_editor
