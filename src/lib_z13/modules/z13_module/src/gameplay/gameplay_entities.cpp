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

#include <algorithm>
#include <format>
#include <optional>
#include <utility>
#include <vector>

#include <Eigen/Dense>

#include <lib_core/state/world_state.h>
#include <lib_core/utils/flecs_utils.h>
#include <lib_core/utils/math.h>

#include <z13/components/gameplay.h>
#include <z13/components/input.h>
#include <z13/components/station.h>
#include <z13_settings/physics_tuning.h>

namespace z13::gameplay {

namespace {

// A player closer than this to a spawn point occupies it.
constexpr float kSpawnPointClearance = 1.f;

// The first spawn point, in entity id order, with no player on it; if all are taken, the
// player's id picks one. Nullopt without spawn points.
std::optional<Eigen::Matrix4f> ChooseSpawnPoint(flecs::world world, uint32_t id) {
  std::vector<std::pair<flecs::entity_t, Eigen::Matrix4f>> points;
  world.query_builder<const Eigen::Matrix4f>().with<z13::station::SpawnPoint>().build().each(
      [&points](flecs::entity e, const Eigen::Matrix4f& transform) { points.emplace_back(e.id(), transform); });
  if (points.empty()) {
    return std::nullopt;
  }
  std::ranges::sort(points, {}, &std::pair<flecs::entity_t, Eigen::Matrix4f>::first);

  std::vector<Eigen::Vector3f> players;
  world.query_builder<const Player, const Eigen::Matrix4f>().build().each(
      [&players](const Player&, const Eigen::Matrix4f& transform) {
        players.push_back(z13::math::ExtractTranslation<float>(transform));
      });
  for (const auto& [entity, transform] : points) {
    const Eigen::Vector3f position = z13::math::ExtractTranslation<float>(transform);
    const bool occupied = std::ranges::any_of(players, [&position](const Eigen::Vector3f& player) {
      return (player - position).norm() < kSpawnPointClearance;
    });
    if (!occupied) {
      return transform;
    }
  }
  return points[id % points.size()].second;
}

}  // namespace

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
  if (const auto spawn_point = ChooseSpawnPoint(world, id)) {
    transform = *spawn_point;
  }

  // Not ActionListener: it isn't state, so a client-reconstructed player wouldn't have
  // one either way. gameplay_input_system.cpp's EnsurePlayerActionListener backfills it
  // for every Player uniformly instead.
  return world.entity(PlayerEntityName(id).c_str())
      .add<z13::flecs_tools::StateEntity>()
      .set(std::move(camera))
      .set(transform)
      .set(Player{.id = id})
      .set(PlayerCollider{.radius = world.get<PhysicsTuning>().player_collider_radius});
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
