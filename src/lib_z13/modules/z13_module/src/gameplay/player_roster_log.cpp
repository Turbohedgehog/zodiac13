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

#include "player_roster_log.h"

#include <algorithm>
#include <format>
#include <string>
#include <vector>

#include <Eigen/Core>

#include <lib_core/state/rollback.h>
#include <lib_core/state/world_state.h>
#include <lib_core/time/simulation_clock.h>
#include <lib_core/utils/log.h>
#include <lib_core/utils/math.h>
#include <lib_core/utils/point_text.h>
#include <lib_core/world/lifecycle.h>

#include <z13/components/action_listener.h>
#include <z13/components/player.h>

namespace z13::gameplay {

namespace {

namespace ft = z13::flecs_tools;

constexpr uint64_t kPositionsEverySeconds = 5;

using PlayerQuery = flecs::query<const Player>;

std::string Describe(flecs::world world, const std::vector<LoggedPlayer>& players) {
  std::string text;
  for (const LoggedPlayer& player : players) {
    text += std::format(" {} (id {}, entity {}{})", player.name, player.id, player.entity,
                        player.listener ? ", local listener" : "");
    const flecs::entity entity = world.entity(player.entity);
    if (player.has_transform) {
      const Eigen::Vector3f at = z13::math::ExtractTranslation<float>(entity.get<Eigen::Matrix4f>());
      text += std::format(" at {}", z13::math::PointText(at));
    } else {
      text += " without a transform";
    }
  }
  return text.empty() ? " none" : text;
}

void LogPlayers(flecs::world world, const PlayerQuery& query, PlayerRosterLog& logged, uint64_t tick,
                const LocalPlayer& local) {
  std::vector<LoggedPlayer> players;
  query.each([&players](flecs::entity entity, const Player& player) {
    players.push_back({.entity = entity.id(),
                       .id = player.id,
                       .name = entity.name().c_str(),
                       .listener = entity.has<z13::input::CurrentActionListenerTag>(),
                       .has_transform = entity.has<Eigen::Matrix4f>()});
  });
  std::ranges::sort(players, {}, &LoggedPlayer::id);

  const std::optional<uint64_t> ticks_per_second = ft::TicksPerSecond(world);
  const bool positions_due = ticks_per_second && tick % (*ticks_per_second * kPositionsEverySeconds) == 0;
  if (players == logged.players && !positions_due) {
    return;
  }
  log_debug("Players at tick {} (local id {}):{}", tick, local.id ? std::to_string(*local.id) : "none",
            Describe(world, players));
  logged.players = std::move(players);
}

void RegisterComponents(flecs::world world) {
  ft::RegisterComponent<PlayerRosterLog>(world);
}

void RegisterSystems(flecs::world world) {
  world.set<PlayerRosterLog>({});
  const PlayerQuery query = world.query_builder<const Player>("PlayerRosterLogSystem::PlayerQuery").build();
  world.system<PlayerRosterLog, const ft::SimulationClock, const LocalPlayer>("PlayerRosterLogSystem::LogPlayers")
      .kind(flecs::PostFrame)
      .without<ft::ReplayInProgress>()
      .each([query](flecs::iter& it, size_t, PlayerRosterLog& logged, const ft::SimulationClock& clock,
                    const LocalPlayer& local) { LogPlayers(it.world(), query, logged, clock.tick, local); });
}

}  // namespace

void PlayerRosterLogSystem::Register(flecs::world& world) {
  OnRegisterComponents(world, RegisterComponents);
  OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::gameplay
