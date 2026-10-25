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


#include "player_positions.h"

#include <lib_core/time/simulation_clock.h>

#include <z13/components/player.h>

namespace z13::net {

const PlayerPositions& CurrentPlayerPositions(flecs::world world) {
  const uint64_t tick = world.get<z13::flecs_tools::SimulationClock>().tick;
  auto& positions = world.get_mut<PlayerPositions>();
  if (positions.read_at_tick != tick) {
    positions = {.read_at_tick = tick};
    world.query_builder<const z13::gameplay::Player, const Eigen::Matrix4f>().build().each(
        [&positions](const z13::gameplay::Player& player, const Eigen::Matrix4f& transform) {
          positions.by_player.emplace(player.id, Eigen::Vector3f(transform.block<3, 1>(0, 3)));
        });
  }
  return positions;
}

}  // namespace z13::net
