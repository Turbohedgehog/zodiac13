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
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <flecs.h>

#include <lib_core/state/world_serializer.h>

#include <sectors/interest_map.h>
#include <sectors/sector.h>

#include "interest_view.h"
#include "net_session.h"
#include "wire_records.h"

namespace z13::net {

// What the server has told one client about: the sectors whose blocks it holds and the players
// it has (docs/interest-design.md). Starts as the whole world, which is what a Welcome or Resync gives.
struct ClientKnowledge {
  SectorSet sectors;
  sectors::SectorBox presence;  // the subscribed sectors and the ring around them, where a player's reach can touch
  std::unordered_set<uint32_t> players;
  std::vector<sectors::SectorKey> unsent;  // entered, but the event could not be made yet
  // With net.ghosts, the players chosen to be simulated (ChooseSimulated); the others in the presence box are ghosts.
  std::unordered_set<uint32_t> simulated;
  // The players the client had from each tick on, for the digests of those ticks (under net.ghosts a player
  // can enter or leave the client's sectors at any time).
  std::map<uint64_t, std::unordered_set<uint32_t>> players_from;
  // What each EntityEntered carried, by the tick it was captured in and the player (ResendChangedEntries).
  std::map<uint64_t, std::unordered_map<uint32_t, std::vector<z13::flecs_tools::EntitySnapshot>>> entries;
};

// What InterestMap::Update reported for each client this tick.
using InterestChanges = std::unordered_map<sectors::ClientId, sectors::InterestChange>;

struct InterestKnowledge {
  using Singleton = void;
  using SessionScoped = void;
  std::unordered_map<sectors::ClientId, ClientKnowledge> by_client;
};

// The connections that have `player_id` (and the player's own); every connection when interest events are off.
// A client with no entry yet has the whole world.
std::vector<ConnectionId> KnowingConnections(
    flecs::world world, const NetSession& session, uint32_t player_id, std::optional<ConnectionId> except);

// Sends `envelope`, about `player_id`, to the KnowingConnections.
void SendToKnowing(flecs::world world, NetSession& session, uint32_t player_id, const Envelope& envelope,
                   std::optional<ConnectionId> except = std::nullopt);

// Queues player_id's `run` for the connections SendToKnowing would pick (NetSession::QueueRun).
void RelayCommands(flecs::world world, NetSession& session, uint32_t player_id, uint64_t through_tick,
                   const CommandRun& run, std::optional<ConnectionId> except = std::nullopt);

// The players the client had at `tick`; the current ones for a tick older than the record.
const std::unordered_set<uint32_t>& PlayersAt(const ClientKnowledge& known, uint64_t tick);

// After a Resync: the client holds `view`, or the whole world when it has none.
void RememberResync(flecs::world world, sectors::ClientId client, const std::optional<InterestView>& view);

// Sends every client the sectors and players that entered or left its interest since the last
// call, to apply at the next tick. `moved_players` are those the sector index placed anew meanwhile.
void SendInterestEvents(flecs::world world, const InterestChanges& changes, std::span<const uint64_t> moved_players);

}  // namespace z13::net
