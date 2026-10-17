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

#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_map>

#include <flecs.h>

#include <lib_core/utils/status.h>
#include <z13/components/gameplay.h>
#include <z13/components/action_id.h>

#include <net_module/transport.h>

#include "net_session.h"
#include "wire_records.h"

namespace z13::net {

// Not state: IdCounters grows only when the PlayerJoined applies, and a rollback restores it;
// either would hand an id out twice.
struct PlayerIdAllocator {
  using Singleton = void;
  using SessionScoped = void;
  uint32_t next_player_id {};
};

// Commands a connection may still send, refilled every tick (AllowCommand).
struct ConnectionRateLimit {
  uint64_t refilled_tick {};
  uint64_t commands_left {};
};

struct CommandRateLimits {
  using Singleton = void;
  using SessionScoped = void;
  std::unordered_map<ConnectionId, ConnectionRateLimit> by_connection;
};

struct PlayerActionKey {
  uint32_t player_id {};
  z13::input::ActionId action_id {};
  bool operator==(const PlayerActionKey&) const = default;
};

struct PlayerActionKeyHash {
  size_t operator()(const PlayerActionKey& key) const {
    return std::hash<uint64_t> {}((static_cast<uint64_t>(key.player_id) << 32) ^ key.action_id);
  }
};

struct ScheduleWindow {
  uint64_t earliest {};
  uint64_t latest {};
};

struct ScheduledCommandTick {
  uint64_t tick {};
  bool moved {};
};

// Per player, the CommandBatch.through_tick last received: unlike ConfirmedInputTicks, never raised
// by FinalizeLateInput, so it tells a client which of its own records the server had.
struct ReceivedInputTicks {
  using Singleton = void;
  using SessionScoped = void;
  PlayerTicks by_player;
};

// The newest tick each client's action is scheduled for, so a moved command keeps its order.
struct LastCommandTicks {
  using Singleton = void;
  using SessionScoped = void;
  std::unordered_map<PlayerActionKey, ScheduledCommandTick, PlayerActionKeyHash> by_action;
};

Status ServiceServerSession(flecs::world world, NetSession& session, const z13::gameplay::IdCounters& counters);

// Makes the ticks before the late window final for every player: no command can land there any
// more, so a player whose input stopped short of them gets the actions it held released there
// (sent to everyone, its owner too) and is confirmed up to the window (docs/net-core.md, Н1).
void FinalizeLateInput(NetSession& session, flecs::world world);

}  // namespace z13::net
