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
#include <expected>
#include <functional>
#include <iterator>
#include <memory>
#include <span>
#include <optional>
#include <string>
#include <vector>

#include <Eigen/Core>

#include <lib_core/state/world_serializer.h>
#include <lib_core/state/world_snapshot_history.h>
#include <lib_core/time/simulation_clock.h>
#include <lib_core/utils/log.h>

#include <primitives/placement.h>
#include <sectors/sector_index.h>
#include <z13/components/inventory.h>
#include <z13_module/gameplay/gameplay_entities.h>
#include <z13_settings/building_tuning.h>
#include <z13_settings/net_tuning.h>

#include "catch_up.h"
#include "player_positions.h"
#include "simulated_set.h"
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

// The player's entity and the items it carries, which are entities of their own.
std::expected<ft::WorldSnapshot, std::string> CapturePlayerWithItems(
    const flecs::world& world, flecs::entity player, uint32_t player_id) {
  auto state = ft::CaptureEntityState(world, player);
  if (!state) {
    return state;
  }
  std::vector<flecs::entity> items;
  world.query<const z13::inventory::Item, const z13::inventory::CarriedBy>().each(
      [&](flecs::entity item, const z13::inventory::Item&, const z13::inventory::CarriedBy& carried) {
        if (carried.player_id == player_id) {
          items.push_back(item);
        }
      });
  for (const flecs::entity item : items) {
    auto captured = ft::CaptureEntityState(world, item);
    if (!captured) {
      return captured;
    }
    std::ranges::move(captured->entities, std::back_inserter(state->entities));
  }
  std::ranges::sort(state->entities, {}, &ft::EntitySnapshot::name);
  return state;
}

bool SendEntityEntered(const EventSender& sender, uint32_t player_id) {
  const flecs::entity player = sender.world.lookup(z13::gameplay::PlayerEntityName(player_id).c_str());
  if (!player) {
    return false;
  }
  const auto state = CapturePlayerWithItems(sender.world, player, player_id);
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

// Whether the client should have `player_id`: its reach touches the client's sectors and, under a budget, the
// player is among those it simulates.
bool IsPresent(const sectors::SectorIndex& index, const ClientKnowledge& known, sectors::ClientId client,
               uint32_t player_id, bool budgeted) {
  if (player_id == client || (budgeted && !known.simulated.contains(player_id))) {
    return false;
  }
  const auto sector = index.Locate({.kind = MemberKind::kPlayer, .id = player_id});
  return sector && known.presence.Contains(*sector);
}

// How many max_late_ticks of players' history to keep: a digest is sent that long after its tick, and a bit more.
constexpr uint64_t kHistoryHorizons = 2;

SimulatedBudget BudgetOf(const z13::NetTuning& tuning) {
  return {.max_players = tuning.simulated_players_max,
          .slack = tuning.simulated_players_slack,
          .radius_m = tuning.simulated_radius_m,
          .hysteresis_m = tuning.simulated_hysteresis_m};
}

// Chooses again which of the players near the client it simulates; returns those whose place changed.
std::vector<uint32_t> ReselectSimulated(flecs::world world, sectors::ClientId client, ClientKnowledge& known) {
  const auto& positions = CurrentPlayerPositions(world).by_player;
  const auto own = positions.find(client);
  if (own == positions.end()) {
    return {};
  }
  const auto& index = world.get<sectors::SectorIndex>();
  std::vector<SimulatedCandidate> candidates;
  for (const uint64_t id : index.IdsOf(MemberKind::kPlayer)) {
    const auto player_id = static_cast<uint32_t>(id);
    const auto position = positions.find(player_id);
    if (position != positions.end() && IsPresent(index, known, client, player_id, false)) {
      candidates.push_back({.player_id = player_id, .distance_m = (position->second - own->second).norm()});
    }
  }
  std::unordered_set<uint32_t> chosen =
      ChooseSimulated(candidates, known.simulated, BudgetOf(world.get<z13::NetTuning>()));
  std::vector<uint32_t> changed;
  std::ranges::copy_if(chosen, std::back_inserter(changed), [&](uint32_t id) { return !known.simulated.contains(id); });
  std::ranges::copy_if(known.simulated, std::back_inserter(changed), [&](uint32_t id) { return !chosen.contains(id); });
  known.simulated = std::move(chosen);
  std::ranges::sort(changed);  // the sets are unordered; the events go out in one order every run
  return changed;
}

// Notes that the client has `known.players` from `from_tick` on, and forgets what no digest can ask for.
void RecordPlayers(ClientKnowledge& known, uint64_t from_tick, uint64_t horizon_ticks) {
  known.players_from[from_tick] = known.players;
  const uint64_t horizon = from_tick > horizon_ticks ? from_tick - horizon_ticks : 0;
  auto in_force = known.players_from.upper_bound(horizon);
  if (in_force != known.players_from.begin()) {
    --in_force;
  }
  known.players_from.erase(known.players_from.begin(), in_force);
}

// Sends the player's entry or exit if the client's knowledge of it is out of date.
void SyncPlayer(const EventSender& sender, const sectors::SectorIndex& index, sectors::ClientId client,
                ClientKnowledge& known, uint32_t player_id) {
  const bool present = IsPresent(index, known, client, player_id, sender.world.get<z13::NetTuning>().ghosts);
  const uint64_t horizon_ticks = sender.world.get<z13::NetTuning>().max_late_ticks * kHistoryHorizons;
  if (known.players.contains(player_id)) {
    if (!present) {
      SendEntityLeft(sender, player_id);
      known.players.erase(player_id);
      RecordPlayers(known, sender.apply_tick, horizon_ticks);
    }
  } else if (present && SendEntityEntered(sender, player_id)) {
    known.players.insert(player_id);
    RecordPlayers(known, sender.apply_tick, horizon_ticks);
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

// Sends the sectors the client has entered and has no event for yet; one that fails is tried again next tick.
void SendUnsentSectors(const EventSender& sender, const sectors::InterestMap& map, sectors::ClientId client,
                       ClientKnowledge& known) {
  std::vector<SectorKey> failed;
  for (const SectorKey& sector : known.unsent) {
    if (!map.Covers(client, sector) || known.sectors.contains(sector)) {
      continue;
    }
    if (SendSectorEntered(sender, sector)) {
      known.sectors.insert(sector);
    } else {
      failed.push_back(sector);
    }
  }
  known.unsent = std::move(failed);
}

// A client's sectors change rarely, so everything but the players that moved is worked out only then.
void SyncClient(const EventSender& sender, sectors::ClientId client, ClientKnowledge& known, bool first,
                const sectors::InterestChange& change, std::span<const uint64_t> moved_players) {
  const auto& index = sender.world.get<sectors::SectorIndex>();
  const auto& map = sender.world.get<sectors::InterestMap>();
  const bool sectors_changed = first || !change.entered.empty() || !change.left.empty();
  if (sectors_changed) {
    known.presence = PresenceOf(*map.BoundsOf(client));
    if (first) {
      const std::vector<SectorKey> subscribed = map.SectorsOf(client);
      const SectorSet wanted {subscribed.begin(), subscribed.end()};
      StartKnowledge(index, client, wanted, known);
      std::vector<SectorKey> outside;
      std::ranges::copy_if(known.sectors, std::back_inserter(outside),
                           [&wanted](const SectorKey& sector) { return !wanted.contains(sector); });
      for (const SectorKey& sector : outside) {
        SendSectorLeft(sender, sector);
        known.sectors.erase(sector);
      }
    }
    for (const SectorKey& sector : change.left) {
      if (known.sectors.erase(sector) > 0) {
        SendSectorLeft(sender, sector);
      }
    }
    for (const SectorKey& sector : change.entered) {
      if (!known.sectors.contains(sector)) {
        known.unsent.push_back(sector);
      }
    }
    SendUnsentSectors(sender, map, client, known);
    std::vector<uint32_t> everyone(known.players.begin(), known.players.end());
    for (const uint64_t id : index.IdsOf(MemberKind::kPlayer)) {
      everyone.push_back(static_cast<uint32_t>(id));
    }
    for (const uint32_t id : everyone) {
      SyncPlayer(sender, index, client, known, id);
    }
    return;
  }
  SendUnsentSectors(sender, map, client, known);
  for (const uint64_t id : moved_players) {
    SyncPlayer(sender, index, client, known, static_cast<uint32_t>(id));
  }
}

}  // namespace

std::vector<ConnectionId> KnowingConnections(
    flecs::world world, const NetSession& session, uint32_t player_id, std::optional<ConnectionId> except) {
  std::vector<ConnectionId> recipients;
  if (!world.get<z13::NetTuning>().interest_events) {
    std::ranges::copy_if(session.Connections(), std::back_inserter(recipients),
                         [&](ConnectionId connection) { return connection != except; });
    return recipients;
  }
  const auto& knowledge = world.get<InterestKnowledge>();
  for (const auto& [connection, client] : session.PlayerConnections()) {
    const auto known = knowledge.by_client.find(client);
    if (connection != except &&
        (client == player_id || known == knowledge.by_client.end() || known->second.players.contains(player_id))) {
      recipients.push_back(connection);
    }
  }
  return recipients;
}

void SendToKnowing(flecs::world world, NetSession& session, uint32_t player_id, const Envelope& envelope,
                   std::optional<ConnectionId> except) {
  session.Multicast(Channel::kReliable, envelope, KnowingConnections(world, session, player_id, except));
}

void RelayCommands(flecs::world world, NetSession& session, uint32_t player_id, uint64_t through_tick,
                   const CommandRun& run, std::optional<ConnectionId> except) {
  session.QueueRun(
      KnowingConnections(world, session, player_id, except), ToWire(player_id, through_tick, run), run.commands);
}

const std::unordered_set<uint32_t>& PlayersAt(const ClientKnowledge& known, uint64_t tick) {
  const auto later = known.players_from.upper_bound(tick);
  return later == known.players_from.begin() ? known.players : std::prev(later)->second;
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
  known.simulated = known.players;
  known.players_from = {{0, known.players}};
}

void SendInterestEvents(flecs::world world, const InterestChanges& changes, std::span<const uint64_t> moved_players) {
  if (!world.get<z13::NetTuning>().interest_events) {
    return;
  }
  auto& knowledge = world.get_mut<InterestKnowledge>();
  NetSession& session = world.get_mut<NetSession>();
  std::optional<SectorBlocks> blocks;
  const uint64_t tick = world.get<ft::SimulationClock>().tick;
  const uint64_t apply_tick = tick + 1;
  const auto& tuning = world.get<z13::NetTuning>();
  std::unordered_set<sectors::ClientId> connected;
  for (const auto& [connection, player] : session.PlayerConnections()) {
    connected.insert(player);
    // Until its player is in the index the client has no interest, and "no interest" would clear its world.
    if (world.get<sectors::InterestMap>().SectorCount(player) == 0) {
      continue;
    }
    const auto [at, first] = knowledge.by_client.try_emplace(player);
    const auto change = changes.find(player);
    const EventSender sender {world, std::ref(session), connection, apply_tick, std::ref(blocks)};
    const bool reselect = tuning.ghosts &&
        (first || tick % tuning.simulated_reselect_ticks == 0 ||
         std::ranges::any_of(moved_players, [&](uint64_t id) {
           const auto moved = static_cast<uint32_t>(id);
           return !at->second.simulated.contains(moved) &&
               IsPresent(world.get<sectors::SectorIndex>(), at->second, player, moved, false);
         }));
    const std::vector<uint32_t> reselected =
        reselect ? ReselectSimulated(world, player, at->second) : std::vector<uint32_t> {};
    SyncClient(sender, player, at->second, first,
               change == changes.end() ? sectors::InterestChange {} : change->second, moved_players);
    for (const uint32_t id : reselected) {
      SyncPlayer(sender, world.get<sectors::SectorIndex>(), player, at->second, id);
    }
  }
  std::erase_if(knowledge.by_client, [&](const auto& entry) { return !connected.contains(entry.first); });
}

}  // namespace z13::net
