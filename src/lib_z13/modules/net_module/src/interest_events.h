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
#include <unordered_map>
#include <unordered_set>

#include <flecs.h>

#include <sectors/interest_map.h>
#include <sectors/sector.h>

#include "interest_view.h"
#include "net_session.h"

namespace z13::net {

// What the server has told one client about: the sectors whose blocks it holds and the players
// it has (docs/interest-design.md). Starts as the whole world, which is what a Welcome or Resync gives.
struct ClientKnowledge {
  SectorSet sectors;
  SectorSet presence;  // the subscribed sectors and the ring around them, where a player's reach can touch
  std::unordered_set<uint32_t> players;
};

struct InterestKnowledge {
  using Singleton = void;
  using SessionScoped = void;
  std::unordered_map<sectors::ClientId, ClientKnowledge> by_client;
};

// Sends `envelope`, about `player_id`, to the clients that have the player, and to the player's own;
// to every connection when interest events are off. A client with no entry yet has the whole world.
void SendToKnowing(flecs::world world, NetSession& session, uint32_t player_id, const Envelope& envelope,
                   std::optional<ConnectionId> except = std::nullopt);

// After a Resync: the client holds `view`, or the whole world when it has none.
void RememberResync(flecs::world world, sectors::ClientId client, const std::optional<InterestView>& view);

// Sends every client the sectors and players that entered or left its interest since the last
// call, to apply at the next tick.
void SendInterestEvents(flecs::world world);

}  // namespace z13::net
