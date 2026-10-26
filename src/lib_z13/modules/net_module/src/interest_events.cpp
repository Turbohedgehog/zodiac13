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
#include <optional>
#include <vector>

#include <Eigen/Core>

#include <lib_core/state/world_serializer.h>
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
using SectorSet = ClientKnowledge::SectorSet;

// A player's reach is within one sector (enforced by net.interest_radius_sectors' rule).
constexpr int kPresenceRing = 1;

// An event applies at the next tick: the state it carries is this tick's, after its systems.
struct EventSender {
  flecs::world world;
  std::reference_wrapper<NetSession> session;
  ConnectionId connection {};
  uint64_t apply_tick {};
  std::reference_wrapper<std::optional<ft::WorldSnapshot>> captured;

  template <typename Body>
  void Send(Body body) const {
    Envelope envelope;
    envelope.body.Set(std::move(body));
    session.get().Send(connection, Channel::kReliable, envelope);
  }

  const ft::WorldSnapshot* State() const {
    std::optional<ft::WorldSnapshot>& state = captured.get();
    if (!state) {
      auto snapshot = ft::CaptureState(world);
      if (!snapshot) {
        log_error("NetSession(server): can't capture the state for an interest event: {}", snapshot.error());
        return nullptr;
      }
      state = std::move(*snapshot);
    }
    return &*state;
  }
};

bool SendSectorEntered(const EventSender& sender, const SectorKey& sector) {
  const ft::WorldSnapshot* state = sender.State();
  if (!state) {
    return false;
  }
  const int chunk_cells = sender.world.get<z13::BuildingTuning>().index_chunk_cells;
  const int sector_cells = sender.world.get<z13::NetTuning>().sector_size_cells;
  ft::WorldSnapshot blocks;
  std::ranges::copy_if(state->groups, std::back_inserter(blocks.groups), [&](const auto& group) {
    return sectors::SectorOf(z13::building::primitives::UnpackChunk(group->id) * chunk_cells, sector_cells) == sector;
  });
  fbn::SectorEnteredT entered;
  entered.apply_tick = sender.apply_tick;
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

SectorSet Dilated(const SectorSet& wanted) {
  SectorSet ring;
  for (const SectorKey& sector : wanted) {
    for (int dx = -kPresenceRing; dx <= kPresenceRing; ++dx) {
      for (int dy = -kPresenceRing; dy <= kPresenceRing; ++dy) {
        for (int dz = -kPresenceRing; dz <= kPresenceRing; ++dz) {
          ring.insert(sector + SectorKey(dx, dy, dz));
        }
      }
    }
  }
  return ring;
}

void SyncClient(const EventSender& sender, sectors::ClientId client, ClientKnowledge& known, bool first) {
  const auto& map = sender.world.get<sectors::InterestMap>();
  const auto& index = sender.world.get<sectors::SectorIndex>();
  const std::vector<SectorKey> subscribed = map.SectorsOf(client);
  const SectorSet wanted {subscribed.begin(), subscribed.end()};

  if (first) {
    const std::vector<SectorKey> occupied = index.Occupied();
    known.sectors = {occupied.begin(), occupied.end()};
    known.sectors.insert(wanted.begin(), wanted.end());
    for (const uint64_t id : index.IdsOf(MemberKind::kPlayer)) {
      known.players.insert(static_cast<uint32_t>(id));
    }
    known.players.erase(client);
    known.presence = Dilated(wanted);
  }

  std::vector<SectorKey> left;
  std::ranges::copy_if(known.sectors, std::back_inserter(left), [&](const SectorKey& s) { return !wanted.contains(s); });
  std::vector<SectorKey> entered;
  std::ranges::copy_if(wanted, std::back_inserter(entered), [&](const SectorKey& s) { return !known.sectors.contains(s); });
  if (!left.empty() || !entered.empty()) {
    known.presence = Dilated(wanted);
  }
  for (const SectorKey& sector : left) {
    SendSectorLeft(sender, sector);
    known.sectors.erase(sector);
  }
  for (const SectorKey& sector : entered) {
    if (SendSectorEntered(sender, sector)) {
      known.sectors.insert(sector);
    }
  }

  std::unordered_set<uint32_t> present;
  for (const SectorKey& sector : known.presence) {
    for (const sectors::SectorMember& member : index.MembersIn(sector)) {
      if (member.kind == MemberKind::kPlayer && member.id != client) {
        present.insert(static_cast<uint32_t>(member.id));
      }
    }
  }
  std::vector<uint32_t> gone;
  std::ranges::copy_if(known.players, std::back_inserter(gone), [&](uint32_t id) { return !present.contains(id); });
  for (const uint32_t id : gone) {
    SendEntityLeft(sender, id);
    known.players.erase(id);
  }
  for (const uint32_t id : present) {
    if (!known.players.contains(id) && SendEntityEntered(sender, id)) {
      known.players.insert(id);
    }
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

void SendInterestEvents(flecs::world world) {
  if (!world.get<z13::NetTuning>().interest_events) {
    return;
  }
  auto& knowledge = world.get_mut<InterestKnowledge>();
  NetSession& session = world.get_mut<NetSession>();
  std::optional<ft::WorldSnapshot> captured;
  const uint64_t apply_tick = world.get<ft::SimulationClock>().tick + 1;
  std::unordered_set<sectors::ClientId> connected;
  for (const auto& [connection, player] : session.PlayerConnections()) {
    connected.insert(player);
    // Until its player is in the index the client has no interest, and "no interest" would clear its world.
    if (world.get<sectors::InterestMap>().SectorCount(player) == 0) {
      continue;
    }
    const auto [at, first] = knowledge.by_client.try_emplace(player);
    SyncClient({world, std::ref(session), connection, apply_tick, std::ref(captured)}, player, at->second, first);
  }
  std::erase_if(knowledge.by_client, [&](const auto& entry) { return !connected.contains(entry.first); });
}

}  // namespace z13::net
