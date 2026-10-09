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

#pragma once

#include <compare>
#include <cstdint>
#include <map>
#include <unordered_map>

#include <flecs.h>

#include <lib_core/utils/status.h>
#include <z13/components/gameplay.h>
#include <z13/components/input.h>

#include <net_module/transport.h>

#include "net_session.h"

namespace z13::net {

// Not state: a rollback restores IdCounters and would hand an id out twice.
struct PlayerIdAllocator {
  using Singleton = void;
  using SessionScoped = void;
  uint32_t next_player_id {};
};

struct ConnectionRateLimit {
  uint64_t window_start_tick {};
  uint32_t commands_this_window {};
};

struct CommandRateLimits {
  using Singleton = void;
  using SessionScoped = void;
  std::unordered_map<ConnectionId, ConnectionRateLimit> by_connection;
};

struct PlayerActionKey {
  uint32_t player_id {};
  z13::input::ActionInfo::IdType action_id {};
  auto operator<=>(const PlayerActionKey&) const = default;
};

struct ScheduledCommandTick {
  uint64_t tick {};
  bool moved {};
};

// The newest tick each client's action is scheduled for, so a moved command keeps its order.
struct LastCommandTicks {
  using Singleton = void;
  using SessionScoped = void;
  std::map<PlayerActionKey, ScheduledCommandTick> by_action;
};

Status ServiceServerSession(flecs::world world, NetSession& session, z13::gameplay::IdCounters& counters);

}  // namespace z13::net
