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

#include <cstdint>
#include <optional>
#include <span>

#include <boost/container/flat_map.hpp>
#include <flecs.h>

#include <net_module/transport.h>
#include <z13/components/action_id.h>

#include "hint_packet.h"
#include "net_session.h"

namespace z13::net {

// What this player last sent as a hint, per action: a hint goes out when the value changes, and
// once more an interval later so a lost one doesn't leave observers on a stale angle.
struct SentActionHint {
  float value {};
  bool repeated {true};
};

struct SentActionHints {
  using Singleton = void;
  using SessionScoped = void;
  boost::container::flat_map<z13::input::ActionId, SentActionHint> by_action;
};

// Server: the tick of each player's newest hints, so a stale or repeated packet isn't relayed.
struct ReceivedHintTicks {
  using Singleton = void;
  using SessionScoped = void;
  boost::container::flat_map<uint32_t, uint64_t> by_player;
};

class ActionHintSystem {
 public:
  // The sender of the local player's hints (every role, the host's included).
  static void Register(flecs::world& world);
};

// Server: a client's hint packet, checked and relayed to the clients the action's class reaches.
void HandleHintPacket(
    NetSession& session, flecs::world world, ConnectionId connection, std::span<const uint8_t> bytes);

// Server: `packet` about `packet.player_id` to every client, bar `except`, its class's filter lets through.
void RelayActionHints(
    NetSession& session, flecs::world world, std::optional<ConnectionId> except, const HintPacket& packet);

// Client: what the server relayed about another player, for the prediction of its unconfirmed ticks.
void StoreRelayedHints(flecs::world world, std::span<const uint8_t> bytes);

}  // namespace z13::net
