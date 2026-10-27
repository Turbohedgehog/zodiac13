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

#include "action_hints.h"

#include <cstdint>
#include <limits>
#include <map>
#include <string_view>
#include <vector>

#include <lib_core/state/rollback.h>
#include <lib_core/state/world_state.h>
#include <lib_core/time/simulation_clock.h>
#include <lib_core/utils/flecs_utils.h>
#include <lib_core/world/lifecycle.h>
#include <lib_profile/profile.h>

#include <z13/components/action_hints.h>
#include <z13/components/action_map.h>
#include <z13/components/net.h>
#include <z13/components/player.h>
#include <z13/components/player_action.h>
#include <z13_settings/net_tuning.h>

#include "hint_classes.h"
#include "player_positions.h"

namespace z13::net {

namespace {

namespace ft = z13::flecs_tools;
namespace fbn = fbs::net;

std::optional<std::string_view> HintClassOf(const z13::input::ActionMap& action_map, uint16_t action_id) {
  const auto& by_id = action_map.action_map.get<z13::input::ActionMap::IdTag>();
  const auto info = by_id.find(static_cast<z13::input::ActionId>(action_id));
  return info == by_id.end() ? std::nullopt : info->hint_class;
}

// `tick` is the hint packet's, a multiple of the class's interval: the far band takes every far_every-th.
bool Reaches(const HintClassTuning& tuning, flecs::world world, uint32_t subject, uint32_t recipient, uint64_t tick) {
  switch (tuning.filter) {
    case fbn::HintFilter::None:
      return false;
    case fbn::HintFilter::All:
      return true;
    case fbn::HintFilter::Distance: {
      const auto& by_player = CurrentPlayerPositions(world).by_player;
      const auto from = by_player.find(subject);
      const auto to = by_player.find(recipient);
      if (from == by_player.end() || to == by_player.end()) {
        return false;
      }
      const double distance = (from->second - to->second).norm();
      return distance <= tuning.near_radius_m ||
          (distance <= tuning.radius_m && tick / tuning.interval_ticks % tuning.far_every == 0);
    }
  }
  return false;
}

bool IsDue(const std::optional<HintClassTuning>& hint_class, uint64_t tick) {
  return hint_class && hint_class->filter != fbn::HintFilter::None && tick % hint_class->interval_ticks == 0;
}

void SendOwnHints(
    flecs::iter& it, size_t, NetSession& session, const ft::SimulationClock& clock,
    const z13::gameplay::LocalPlayer& local_player, SentActionHints& sent,
    const z13::gameplay::LastRecordedActionValues& last_recorded, const z13::input::ActionMap& action_map,
    const z13::NetTuning& tuning) {
  const bool is_server = it.world().has<ServerRole>();
  if (!local_player.id || (!is_server && !session.ServerConnection())) {
    return;
  }
  HintPacket packet {.player_id = *local_player.id, .tick = clock.tick};
  for (const z13::input::ActionInfo& info : action_map.action_map.get<z13::input::ActionMap::IdTag>()) {
    if (!info.hint_class || info.id > std::numeric_limits<uint16_t>::max()) {
      continue;
    }
    const auto value = last_recorded.values.find(info.id);
    if (!IsDue(FindHintClass(tuning, *info.hint_class), clock.tick) || value == last_recorded.values.end()) {
      continue;
    }
    SentActionHint& was = sent.by_action[info.id];
    if (value->second != was.value) {
      was = {.value = value->second, .repeated = false};
    } else if (was.repeated) {
      continue;
    } else {
      was.repeated = true;
    }
    packet.hints.push_back(
        {.action_id = static_cast<uint16_t>(info.id), .value = z13::gameplay::QuantizeActionValue(value->second)});
  }
  if (packet.hints.empty()) {
    return;
  }
  if (is_server) {
    RelayActionHints(session, it.world(), std::nullopt, packet);
    return;
  }
  session.SendBytes(*session.ServerConnection(), Channel::kHints, EncodeOwnHints(packet));
}

void RegisterComponents(flecs::world world) {
  z13::flecs_tools::RegisterComponents<SentActionHints, ReceivedHintTicks, PlayerPositions>(world);
}

void RegisterSystems(flecs::world world) {
  // Set here, not next to `.add(flecs::Singleton)` (see PhysicsSystem::RegisterSystems).
  world.set<SentActionHints>({});
  world.set<ReceivedHintTicks>({});
  world.set<PlayerPositions>({});
  world.system<
      NetSession, const ft::SimulationClock, const z13::gameplay::LocalPlayer, SentActionHints,
      const z13::gameplay::LastRecordedActionValues, const z13::input::ActionMap, const z13::NetTuning>(
      "ActionHintSystem::SendOwnHints")
      .kind(flecs::PostUpdate)
      .without<ft::ReplayInProgress>()
      .each(SendOwnHints);
}

}  // namespace

void ActionHintSystem::Register(flecs::world& world) {
  z13::OnRegisterComponents(world, RegisterComponents);
  z13::OnInitSystems(world, RegisterSystems);
}

void HandleHintPacket(
    NetSession& session, flecs::world world, ConnectionId connection, std::span<const uint8_t> bytes) {
  Z13_PROFILE_ZONE("HandleHintPacket");
  const std::optional<uint32_t> player_id = session.PlayerIdFor(connection);
  const uint64_t now = world.get<ft::SimulationClock>().tick;
  auto decoded = DecodeOwnHints(bytes, now);
  if (!player_id || !decoded) {
    return;
  }
  const auto& tuning = world.get<z13::NetTuning>();
  // Capped like a command's tick, and each packet newer than the last: a client can't flood the others.
  if (decoded->tick > now + tuning.max_schedule_ahead_ticks) {
    return;
  }
  auto& received = world.get_mut<ReceivedHintTicks>().by_player;
  const auto [entry, inserted] = received.try_emplace(*player_id, decoded->tick);
  if (!inserted && decoded->tick <= entry->second) {
    return;
  }
  entry->second = decoded->tick;
  decoded->player_id = *player_id;
  RelayActionHints(session, world, connection, *decoded);
}

void RelayActionHints(
    NetSession& session, flecs::world world, std::optional<ConnectionId> except, const HintPacket& packet) {
  Z13_PROFILE_ZONE("RelayActionHints");
  const auto& tuning = world.get<z13::NetTuning>();
  const auto& action_map = world.get<z13::input::ActionMap>();
  std::map<std::string_view, HintPacket> by_class;
  for (const HintRecord& hint : packet.hints) {
    if (const auto hint_class = HintClassOf(action_map, hint.action_id)) {
      const HintPacket empty {.player_id = packet.player_id, .tick = packet.tick};
      by_class.try_emplace(*hint_class, empty).first->second.hints.push_back(hint);
    }
  }
  const auto recipients = session.PlayerConnections();
  for (const auto& [name, part] : by_class) {
    const auto hint_class = FindHintClass(tuning, name);
    if (!hint_class) {
      continue;
    }
    const std::vector<uint8_t> bytes = EncodeRelayedHints(part);
    for (const auto& [connection, player_id] : recipients) {
      if (connection != except && player_id != packet.player_id &&
          Reaches(*hint_class, world, packet.player_id, player_id, packet.tick)) {
        session.SendBytes(connection, Channel::kHints, bytes);
      }
    }
  }
}

void StoreRelayedHints(flecs::world world, std::span<const uint8_t> bytes) {
  const auto decoded = DecodeRelayedHints(bytes, world.get<ft::SimulationClock>().tick);
  if (!decoded || decoded->player_id == world.get<z13::gameplay::LocalPlayer>().id) {
    return;
  }
  const auto& action_map = world.get<z13::input::ActionMap>();
  auto& store = world.get_mut<z13::gameplay::ActionHints>();
  for (const HintRecord& hint : decoded->hints) {
    if (HintClassOf(action_map, hint.action_id)) {
      store.Add(
          decoded->player_id, hint.action_id,
          {.tick = decoded->tick, .value = z13::gameplay::DequantizeActionValue(hint.value)});
    }
  }
}

}  // namespace z13::net
