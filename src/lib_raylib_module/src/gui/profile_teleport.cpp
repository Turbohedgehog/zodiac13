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

#include <cmath>
#include <optional>

#include <lib_core/utils/camera_pose.h>
#include <lib_core/utils/math.h>
#include <z13/components/gameplay/net.h>
#include <z13/components/gameplay/player.h>
#include <z13/components/gameplay/player_parts.h>
#include <z13/components/input/input_state.h>

namespace z13::raylib::gui {

namespace {

// The way round of at most half a turn.
float WrapDegrees(float degrees) {
  constexpr float kTurn = 360.f;
  return degrees - (kTurn * std::round(degrees / kTurn));
}

}  // namespace

z13::Status TeleportLocalPlayer(flecs::world world, const Eigen::Vector2f& position_m,
                                std::optional<float> height_m, const LookTarget& look) {
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
  const float height = height_m.value_or(z13::math::ExtractTranslation<float>(transform).z());
  z13::math::SetTranslation(Eigen::Vector3f(position_m.x(), position_m.y(), height), transform);
  own->set(transform);
  if (look.yaw_deg || look.pitch_deg) {
    // Turned as the mouse does, so the look reaches the simulation by the input path.
    const z13::LookAngles current = own->has<z13::LookAngles>() ? own->get<z13::LookAngles>() : z13::LookAngles {};
    auto& input = world.get_mut<z13::input::InputState>();
    input.mouse_yaw_delta_deg += WrapDegrees(look.yaw_deg.value_or(current.yaw_deg) - current.yaw_deg);
    // TurnLook subtracts the pitch delta.
    input.mouse_pitch_delta_deg += current.pitch_deg - look.pitch_deg.value_or(current.pitch_deg);
  }
  z13::gameplay::PlayerMotion motion = own->get<z13::gameplay::PlayerMotion>();
  motion.fall_speed = 0.f;
  own->set(motion);
  return {};
}

}  // namespace z13::raylib::gui
