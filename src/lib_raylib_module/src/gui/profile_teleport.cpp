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

#include "profile_teleport.h"

#include <optional>

#include <lib_core/utils/math.h>
#include <z13/components/net.h>
#include <z13/components/player.h>
#include <z13/components/player_parts.h>

namespace z13::raylib::gui {

z13::Status TeleportLocalPlayer(flecs::world world, const Eigen::Vector2f& position_m) {
  const auto* local = world.try_get<z13::gameplay::LocalPlayer>();
  if (local == nullptr || !local->id) {
    return std::unexpected("there is no local player");
  }
  const auto* connection = world.try_get<z13::net::ConnectionStatus>();
  if (connection != nullptr && connection->state != z13::net::ConnectionState::kNone) {
    return std::unexpected("not while connected to a server");
  }
  size_t players = 0;
  std::optional<flecs::entity> own;
  world.query_builder<const z13::gameplay::Player>().build().each(
      [&](flecs::entity player, const z13::gameplay::Player& candidate) {
        ++players;
        if (candidate.id == *local->id) {
          own = player;
        }
      });
  if (!own) {
    return std::unexpected("the local player isn't in the game");
  }
  if (players > 1) {
    return std::unexpected("other players are in the game");
  }
  Eigen::Matrix4f transform = own->get<Eigen::Matrix4f>();
  const float height = z13::math::ExtractTranslation<float>(transform).z();
  z13::math::SetTranslation(Eigen::Vector3f(position_m.x(), position_m.y(), height), transform);
  own->set(transform);
  z13::gameplay::PlayerMotion motion = own->get<z13::gameplay::PlayerMotion>();
  motion.fall_speed = 0.f;
  own->set(motion);
  return {};
}

}  // namespace z13::raylib::gui
