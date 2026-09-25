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

#include <z13_module/gameplay/gameplay_entities.h>

#include <format>
#include <optional>

#include <Eigen/Dense>

#include <lib_core/flecs_utils.h>
#include <lib_core/world_state.h>

#include <z13/components/gameplay.h>
#include <z13/components/input.h>

namespace z13::gameplay {

std::string PlayerEntityName(uint32_t id) {
  return std::format("Player_{}", id);
}

flecs::entity SpawnPlayer(flecs::world world, uint32_t id) {
  Camera camera {
    .fov = 90,
    .name = std::format("PlayerCamera_{}", id),
  };

  Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
  transform(0, 3) = static_cast<float>(id) * kSpawnSpacing;

  // Not ActionListener: it isn't state, so a client-reconstructed player wouldn't have
  // one either way. gameplay_input_system.cpp's EnsurePlayerActionListener backfills it
  // for every Player uniformly instead.
  return world.entity(PlayerEntityName(id).c_str())
      .add<z13::flecs_tools::StateEntity>()
      .set(std::move(camera))
      .set(transform)
      .set(Player{.id = id})
      .set(PlayerCollider{.radius = kPlayerColliderRadius});
}

void EnsureLocalPlayerReady(flecs::world world) {
  const std::optional<uint32_t> local_id = world.get<LocalPlayer>().id;
  if (!local_id) {
    return;
  }
  const flecs::entity player = world.lookup(PlayerEntityName(*local_id).c_str());
  if (!player) {
    return;
  }

  if (!player.has<z13::input::ActionListener>()) {
    player.set(z13::input::ActionListener{.action_group_priority = {std::string(z13::input::kControlActionGroup)}});
  }
  player.add<z13::input::InputListener>();
  player.add<z13::input::CurrentActionListenerTag>();
}

}  // namespace z13::gameplay
