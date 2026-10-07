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

#include "session_deltas.h"

#include <algorithm>
#include <iterator>
#include <ranges>
#include <utility>

#include <lib_core/state/rollback.h>
#include <lib_core/state/world_serializer.h>
#include <lib_core/state/world_snapshot_history.h>
#include <lib_core/time/simulation_clock.h>
#include <lib_core/utils/log.h>
#include <lib_core/utils/math.h>

#include <z13/components/gameplay.h>
#include <z13/components/player_action.h>
#include <z13_module/gameplay/gameplay_entities.h>
#include <z13_settings/net_tuning.h>

#include "wire_records.h"

namespace z13::net {

namespace {

namespace ft = z13::flecs_tools;
namespace fbn = fbs::net;

// Keeps IdCounters in step with the server, which bumped it at ClientHello.
void ApplyPlayerJoined(flecs::world world, const fbn::PlayerJoinedT& joined) {
  if (!joined.entity_state) {
    log_warn("NetSession: PlayerJoined without the player's state");
    return;
  }
  if (auto applied = ft::ApplyWorldStateDelta(world, ft::FromFlatbuffer(*joined.entity_state)); !applied) {
    log_warn("NetSession: PlayerJoined delta rejected: {}", applied.error());
    return;
  }
  if (!world.has<z13::gameplay::IdCounters>()) {
    return;
  }
  auto& counters = world.get_mut<z13::gameplay::IdCounters>();
  world.query_builder<const z13::gameplay::Player>().build().each(
      [&counters](const z13::gameplay::Player& player) {
        counters.last_player_id = std::max(counters.last_player_id, player.id + 1);
      });
}

void ApplyPlayerLeft(flecs::world world, const fbn::PlayerLeftT& left) {
  if (const flecs::entity e = world.lookup(z13::gameplay::PlayerEntityName(left.player_id).c_str())) {
    e.destruct();
  }
}

void ApplySessionDelta(flecs::world world, const SessionDelta& delta) {
  if (const auto* joined = std::get_if<fbn::PlayerJoinedT>(&delta)) {
    ApplyPlayerJoined(world, *joined);
  } else if (const auto* left = std::get_if<fbn::PlayerLeftT>(&delta)) {
    ApplyPlayerLeft(world, *left);
  }
}

void PruneSessionHistory(flecs::world world, uint64_t now, std::vector<ScheduledSessionDelta>& history) {
  const std::optional<uint64_t> retention_ticks = ft::SnapshotRetentionTicks(world);
  if (!retention_ticks) {
    return;
  }
  auto& confirmed = world.get_mut<z13::gameplay::ConfirmedInputTicks>().by_player;
  std::erase_if(history, [&confirmed, now, retention_ticks = *retention_ticks](const ScheduledSessionDelta& item) {
    if (item.apply_tick + retention_ticks >= now) {
      return false;
    }
    // Not at the leave itself: a rollback across it must still see the player's confirmed tick.
    if (const auto* left = std::get_if<fbn::PlayerLeftT>(&item.delta)) {
      confirmed.erase(left->player_id);
    }
    return true;
  });
}

}  // namespace

void ScheduleSessionDelta(
    NetSession& session, flecs::world world, uint64_t apply_tick, SessionDelta delta,
    std::optional<Eigen::Vector3f> spawn_position) {
  world.get_mut<ScheduledSessionDeltas>().pending.push_back(
      {.apply_tick = apply_tick, .delta = delta, .spawn_position = spawn_position});

  Envelope envelope;
  std::visit([&envelope](auto& body) { envelope.body.Set(std::move(body)); }, delta);
  session.Broadcast(Channel::kReliable, envelope);
}

// Spawned only to serialize it: like everywhere else, it exists from apply_tick on.
Status SchedulePlayerJoined(NetSession& session, flecs::world world, uint32_t player_id) {
  std::vector<Eigen::Vector3f> reserved;
  for (const ScheduledSessionDelta& item : world.get<ScheduledSessionDeltas>().pending) {
    if (item.spawn_position) {
      reserved.push_back(*item.spawn_position);
    }
  }
  const flecs::entity player = z13::gameplay::SpawnPlayer(world, player_id, reserved);
  const Eigen::Vector3f spawn_position = z13::math::ExtractTranslation<float>(player.get<Eigen::Matrix4f>());
  fbn::PlayerJoinedT joined;
  joined.apply_tick = world.get<ft::SimulationClock>().tick + world.get<NetTuning>().session_event_delay_ticks;
  const auto entity_state = ft::CaptureEntityState(world, player);
  player.destruct();
  if (!entity_state) {
    return std::unexpected(entity_state.error());
  }
  joined.entity_state = ToWire(*entity_state);

  const uint64_t apply_tick = joined.apply_tick;
  ScheduleSessionDelta(session, world, apply_tick, std::move(joined), spawn_position);
  return {};
}

uint64_t LeaveApplyTick(flecs::world world, uint32_t player_id) {
  auto after_own_commands = world.get<z13::gameplay::ScheduledCommands>().records |
      std::views::filter([player_id](const auto& record) { return record.player_id == player_id; }) |
      std::views::transform([](const auto& record) { return record.tick + 1; });
  return std::ranges::fold_left(
      after_own_commands, world.get<ft::SimulationClock>().tick + world.get<NetTuning>().session_event_delay_ticks,
      [](uint64_t a, uint64_t b) { return std::max(a, b); });
}

void SendSessionDeltas(NetSession& session, ConnectionId connection, std::span<const ScheduledSessionDelta> deltas) {
  for (const ScheduledSessionDelta& item : deltas) {
    Envelope envelope;
    std::visit([&envelope](auto body) { envelope.body.Set(std::move(body)); }, item.delta);
    session.Send(connection, Channel::kReliable, envelope);
  }
}

void ApplyDueSessionDeltas(flecs::world world) {
  auto& scheduled = world.get_mut<ScheduledSessionDeltas>();
  const uint64_t now = world.get<ft::SimulationClock>().tick;

  // Stable, so same-tick deltas keep their arrival order in history.
  auto& pending = scheduled.pending;
  const auto not_due = std::ranges::stable_partition(
      pending, [now](const ScheduledSessionDelta& item) { return item.apply_tick <= now; });
  const auto due_now = std::ranges::subrange(pending.begin(), not_due.begin());
  const uint64_t earliest_due = std::ranges::fold_left(
      due_now | std::views::transform(&ScheduledSessionDelta::apply_tick), now,
      [](uint64_t a, uint64_t b) { return std::min(a, b); });
  std::ranges::for_each(due_now, [&history = scheduled.history](ScheduledSessionDelta& item) {
    const auto at = std::ranges::upper_bound(history, item.apply_tick, {}, &ScheduledSessionDelta::apply_tick);
    history.insert(at, std::move(item));
  });
  pending.erase(pending.begin(), not_due.begin());
  PruneSessionHistory(world, now, scheduled.history);

  // Applying creates/destroys entities, so not while iterating this component.
  const auto applied_now =
      std::ranges::equal_range(scheduled.history, now, {}, &ScheduledSessionDelta::apply_tick);
  std::vector<SessionDelta> due;
  std::ranges::transform(applied_now, std::back_inserter(due), &ScheduledSessionDelta::delta);
  std::ranges::for_each(due, [world](const SessionDelta& delta) { ApplySessionDelta(world, delta); });

  if (earliest_due < now && earliest_due > 0) {
    ft::RequestRollback(world, earliest_due - 1, now);
  }
}

}  // namespace z13::net
