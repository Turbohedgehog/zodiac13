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

#include "interest_events.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <span>
#include <optional>
#include <vector>

#include <Eigen/Core>

#include <lib_core/state/world_serializer.h>
#include <lib_core/state/world_snapshot_history.h>
#include <lib_core/time/simulation_clock.h>
#include <lib_core/utils/log.h>

#include <primitives/placement.h>
#include <sectors/sector_index.h>
#include <z13_module/gameplay/gameplay_entities.h>
#include <z13_settings/building_tuning.h>
#include <z13_settings/net_tuning.h>

#include "catch_up.h"
#include "wire_records.h"

namespace z13::net {

namespace {

namespace ft = z13::flecs_tools;
namespace fbn = fbs::net;
using sectors::MemberKind;
using sectors::SectorKey;

// The blocks of the latest snapshot by sector, built once for every sector entered this tick.
struct SectorBlocks {
  uint64_t tick {};
  std::unordered_map<SectorKey, std::vector<std::shared_ptr<const ft::GroupSnapshot>>, sectors::SectorHash> by_sector;
};

// An event applies at the next tick; a player's state in it is this tick's, after its systems.
struct EventSender {
  flecs::world world;
  std::reference_wrapper<NetSession> session;
  ConnectionId connection {};
  uint64_t apply_tick {};
  std::reference_wrapper<std::optional<SectorBlocks>> blocks;

  template <typename Body>
  void Send(Body body) const {
    Envelope envelope;
    envelope.body.Set(std::move(body));
    session.get().Send(connection, Channel::kReliable, envelope);
  }

  // A sector's blocks come from the snapshot the history already holds (a capture of the whole world
  // costs milliseconds), which the client replays forward from.
  const SectorBlocks* Blocks() const {
    std::optional<SectorBlocks>& built = blocks.get();
    if (built) {
      return &*built;
    }
    const auto& history = world.get<ft::WorldSnapshotHistory>().history;
    std::optional<ft::TimestampedSnapshot> captured;
    if (history.Empty()) {
      auto snapshot = ft::CaptureState(world);
      if (!snapshot) {
        log_error("NetSession(server): can't capture the state for an interest event: {}", snapshot.error());
        return nullptr;
      }
      captured = ft::TimestampedSnapshot {
          .tick = world.get<ft::SimulationClock>().tick, .snapshot = std::move(*snapshot)};
    }
    const ft::TimestampedSnapshot& source = captured ? *captured : history.Entries().back();
    const int chunk_cells = world.get<z13::BuildingTuning>().index_chunk_cells;
    const int sector_cells = world.get<z13::NetTuning>().sector_size_cells;
    built.emplace().tick = source.tick;
    for (const auto& group : source.snapshot.groups) {
      const SectorKey sector =
          sectors::SectorOf(z13::building::primitives::UnpackChunk(group->id) * chunk_cells, sector_cells);
      built->by_sector[sector].push_back(group);
    }
    return &*built;
  }
};

bool SendSectorEntered(const EventSender& sender, const SectorKey& sector) {
  const SectorBlocks* source = sender.Blocks();
  if (!source) {
    return false;
  }
  ft::WorldSnapshot blocks;
  if (const auto held = source->by_sector.find(sector); held != source->by_sector.end()) {
    blocks.groups = held->second;
  }
  fbn::SectorEnteredT entered;
  entered.apply_tick = source->tick + 1;
  entered.sector_x = sector.x();
  entered.sector_y = sector.y();
  entered.sector_z = sector.z();
  entered.blocks = ToWire(blocks);
  sender.Send(std::move(entered));
  return true;
}

void SendSectorLeft(const EventSender& sender, const SectorKey& sector) {
  fbn::SectorLeftT left;
  left.apply_tick = sender.apply_tick;
  left.sector_x = sector.x();
  left.sector_y = sector.y();
  left.sector_z = sector.z();
  sender.Send(std::move(left));
}

bool SendEntityEntered(const EventSender& sender, uint32_t player_id) {
  const flecs::entity player = sender.world.lookup(z13::gameplay::PlayerEntityName(player_id).c_str());
  if (!player) {
    return false;
  }
  const auto state = ft::CaptureEntityState(sender.world, player);
  if (!state) {
    log_error("NetSession(server): can't capture player {} for an interest event: {}", player_id, state.error());
    return false;
  }
  fbn::EntityEnteredT entered;
  entered.apply_tick = sender.apply_tick;
  entered.entity_state = ToWire(*state);
  const PlayerInput input = InputOf(sender.world, player_id, sender.apply_tick - 1);
  entered.held_values = ToWire(input.held_values);
  entered.pending = ToWire(input.pending);
  entered.input_through = ToWire(input.confirmed.by_player);
  sender.Send(std::move(entered));
  return true;
}

void SendEntityLeft(const EventSender& sender, uint32_t player_id) {
  fbn::EntityLeftT left;
  left.apply_tick = sender.apply_tick;
  left.player_id = player_id;
  sender.Send(std::move(left));
}

// Whether the client should have `player_id`: its reach touches the client's sectors.
bool IsPresent(const sectors::SectorIndex& index, const ClientKnowledge& known, sectors::ClientId client,
               uint32_t player_id) {
  if (player_id == client) {
    return false;
  }
  const auto sector = index.Locate({.kind = MemberKind::kPlayer, .id = player_id});
  return sector && known.presence.contains(*sector);
}

// Sends the player's entry or exit if the client's knowledge of it is out of date.
void SyncPlayer(const EventSender& sender, const sectors::SectorIndex& index, sectors::ClientId client,
                ClientKnowledge& known, uint32_t player_id) {
  const bool present = IsPresent(index, known, client, player_id);
  if (known.players.contains(player_id)) {
    if (!present) {
      SendEntityLeft(sender, player_id);
      known.players.erase(player_id);
    }
  } else if (present && SendEntityEntered(sender, player_id)) {
    known.players.insert(player_id);
  }
}

// What the client has been told, from nothing: the whole world, as a Welcome or Resync leaves it.
void StartKnowledge(const sectors::SectorIndex& index, sectors::ClientId client, const SectorSet& wanted,
                    ClientKnowledge& known) {
  const std::vector<SectorKey> occupied = index.Occupied();
  known.sectors = {occupied.begin(), occupied.end()};
  known.sectors.insert(wanted.begin(), wanted.end());
  for (const uint64_t id : index.IdsOf(MemberKind::kPlayer)) {
    known.players.insert(static_cast<uint32_t>(id));
  }
  known.players.erase(client);
}

// A client's sectors change rarely, so everything but the players that moved is worked out only then.
void SyncClient(const EventSender& sender, sectors::ClientId client, ClientKnowledge& known, bool first,
                const sectors::InterestChange& change, std::span<const uint64_t> moved_players) {
  const auto& index = sender.world.get<sectors::SectorIndex>();
  const bool sectors_changed = first || !change.entered.empty() || !change.left.empty();
  if (sectors_changed) {
    const std::vector<SectorKey> subscribed = sender.world.get<sectors::InterestMap>().SectorsOf(client);
    const SectorSet wanted {subscribed.begin(), subscribed.end()};
    if (first) {
      StartKnowledge(index, client, wanted, known);
    }
    known.presence = Dilated(wanted);
    std::vector<SectorKey> left;
    std::ranges::copy_if(known.sectors, std::back_inserter(left),
                         [&wanted](const SectorKey& s) { return !wanted.contains(s); });
    for (const SectorKey& sector : left) {
      SendSectorLeft(sender, sector);
      known.sectors.erase(sector);
    }
    for (const SectorKey& sector : wanted) {
      if (!known.sectors.contains(sector) && SendSectorEntered(sender, sector)) {
        known.sectors.insert(sector);
      }
    }
    std::vector<uint32_t> everyone(known.players.begin(), known.players.end());
    for (const uint64_t id : index.IdsOf(MemberKind::kPlayer)) {
      everyone.push_back(static_cast<uint32_t>(id));
    }
    for (const uint32_t id : everyone) {
      SyncPlayer(sender, index, client, known, id);
    }
    return;
  }
  for (const uint64_t id : moved_players) {
    SyncPlayer(sender, index, client, known, static_cast<uint32_t>(id));
  }
}

}  // namespace

void SendToKnowing(flecs::world world, NetSession& session, uint32_t player_id, const Envelope& envelope,
                   std::optional<ConnectionId> except) {
  if (!world.get<z13::NetTuning>().interest_events) {
    session.Broadcast(Channel::kReliable, envelope, except);
    return;
  }
  const auto& knowledge = world.get<InterestKnowledge>();
  std::vector<ConnectionId> recipients;
  for (const auto& [connection, client] : session.PlayerConnections()) {
    const auto known = knowledge.by_client.find(client);
    if (connection != except &&
        (client == player_id || known == knowledge.by_client.end() || known->second.players.contains(player_id))) {
      recipients.push_back(connection);
    }
  }
  session.Multicast(Channel::kReliable, envelope, recipients);
}

void RememberResync(flecs::world world, sectors::ClientId client, const std::optional<InterestView>& view) {
  auto& by_client = world.get_mut<InterestKnowledge>().by_client;
  if (!view) {
    by_client.erase(client);
    return;
  }
  ClientKnowledge& known = by_client[client];
  known.sectors = view->sectors;
  known.presence = view->presence;
  known.players = view->players;
  known.players.erase(client);
}

void SendInterestEvents(flecs::world world, const InterestChanges& changes, std::span<const uint64_t> moved_players) {
  if (!world.get<z13::NetTuning>().interest_events) {
    return;
  }
  auto& knowledge = world.get_mut<InterestKnowledge>();
  NetSession& session = world.get_mut<NetSession>();
  std::optional<SectorBlocks> blocks;
  const uint64_t apply_tick = world.get<ft::SimulationClock>().tick + 1;
  std::unordered_set<sectors::ClientId> connected;
  for (const auto& [connection, player] : session.PlayerConnections()) {
    connected.insert(player);
    // Until its player is in the index the client has no interest, and "no interest" would clear its world.
    if (world.get<sectors::InterestMap>().SectorCount(player) == 0) {
      continue;
    }
    const auto [at, first] = knowledge.by_client.try_emplace(player);
    const auto change = changes.find(player);
    SyncClient({world, std::ref(session), connection, apply_tick, std::ref(blocks)}, player, at->second, first,
               change == changes.end() ? sectors::InterestChange {} : change->second, moved_players);
  }
  std::erase_if(knowledge.by_client, [&](const auto& entry) { return !connected.contains(entry.first); });
}

}  // namespace z13::net
