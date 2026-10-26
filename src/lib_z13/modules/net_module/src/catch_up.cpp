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

#include "catch_up.h"

#include <algorithm>
#include <iterator>
#include <optional>
#include <ranges>
#include <utility>

#include <boost/container/flat_map.hpp>

#include <lib_core/settings/core_settings.h>
#include <lib_core/state/rollback.h>
#include <lib_core/state/world_snapshot_history.h>
#include <lib_core/time/simulation_clock.h>
#include <lib_core/utils/log.h>
#include <z13_settings/net_tuning.h>

#include <net_module/state_digest.h>
#include <z13/components/action_id.h>

#include "remote_input.h"
#include "scheduled_commands.h"
#include "wire_records.h"

namespace z13::net {

namespace {

namespace ft = z13::flecs_tools;

std::vector<z13::gameplay::PlayerActionRecord> ActionsSince(flecs::world world, uint64_t since_tick) {
  const auto& entries = world.get<z13::gameplay::PlayerActionLog>().log.Entries();
  const auto first = std::ranges::upper_bound(entries, since_tick, {}, &z13::gameplay::PlayerActionRecord::tick);
  return {first, entries.end()};
}

std::vector<z13::gameplay::PlayerActionRecord> HeldValues(flecs::world world, uint64_t snapshot_tick) {
  const auto& entries = world.get<z13::gameplay::PlayerActionLog>().log.Entries();
  const auto up_to_snapshot = std::ranges::subrange(
      entries.begin(), std::ranges::upper_bound(entries, snapshot_tick, {}, &z13::gameplay::PlayerActionRecord::tick));
  boost::container::flat_map<std::pair<uint32_t, z13::input::ActionId>, float> values;
  std::ranges::for_each(up_to_snapshot, [&values](const z13::gameplay::PlayerActionRecord& record) {
    values[{record.player_id, record.action_id}] = record.value;
  });

  std::vector<z13::gameplay::PlayerActionRecord> held;
  std::ranges::copy(
      values | std::views::filter([](const auto& entry) { return entry.second != 0.f; }) |
          std::views::transform([snapshot_tick](const auto& entry) {
            const auto& [key, value] = entry;
            return z13::gameplay::PlayerActionRecord {
                .tick = snapshot_tick, .player_id = key.first, .action_id = key.second, .value = value};
          }),
      std::back_inserter(held));
  return held;
}

// Old enough for a rollback to any command still accepted or awaiting replay, and final: every
// player is confirmed through it, so no confirmation can reach behind it (docs/net-core.md, Н4).
const ft::TimestampedSnapshot& CatchUpBase(
    const ft::WorldSnapshotHistory& history, uint64_t now, std::optional<uint64_t> deferred_rollback_tick,
    uint64_t max_late_ticks, std::optional<uint64_t> all_confirmed) {
  const auto& entries = history.history.Entries();
  const auto settled = std::ranges::find_last_if(entries, [&](const ft::TimestampedSnapshot& entry) {
    return entry.tick + max_late_ticks < now && entry.tick <= deferred_rollback_tick.value_or(entry.tick) &&
           entry.tick <= all_confirmed.value_or(entry.tick);
  });
  return settled.empty() ? entries.front() : settled.front();
}

bool SameRecord(const z13::gameplay::PlayerActionRecord& a, const z13::gameplay::PlayerActionRecord& b) {
  return a.tick == b.tick && a.player_id == b.player_id && a.action_id == b.action_id && a.value == b.value;
}

bool LogHolds(const z13::gameplay::PlayerActionLog& log, const z13::gameplay::PlayerActionRecord& record) {
  const auto same_tick =
      std::ranges::equal_range(log.log.Entries(), record.tick, {}, &z13::gameplay::PlayerActionRecord::tick);
  return std::ranges::any_of(same_tick, [&record](const auto& entry) { return SameRecord(entry, record); });
}

template <typename Predicate>
std::vector<z13::gameplay::PlayerActionRecord> Filtered(
    const std::vector<z13::gameplay::PlayerActionRecord>& records, const Predicate& keep) {
  std::vector<z13::gameplay::PlayerActionRecord> kept;
  std::ranges::copy_if(records, std::back_inserter(kept), keep);
  return kept;
}

}  // namespace

PlayerInput InputOf(flecs::world world, uint32_t player_id, uint64_t snapshot_tick) {
  PlayerInput input;
  std::ranges::copy_if(HeldValues(world, snapshot_tick), std::back_inserter(input.held_values),
                       [player_id](const auto& record) { return record.player_id == player_id; });
  std::ranges::copy_if(world.get<z13::gameplay::ScheduledCommands>().records, std::back_inserter(input.pending),
                       [player_id](const auto& record) { return record.player_id == player_id; });
  const auto& confirmed = world.get<z13::gameplay::ConfirmedInputTicks>().by_player;
  if (const auto through = confirmed.find(player_id); through != confirmed.end()) {
    input.confirmed.by_player.emplace(*through);
  }
  return input;
}

void MergePlayerInput(flecs::world world, PlayerInput input) {
  auto& log = world.get_mut<z13::gameplay::PlayerActionLog>();
  std::vector<z13::gameplay::PlayerActionRecord> missing;
  std::ranges::copy_if(input.held_values, std::back_inserter(missing),
                       [&log](const auto& record) { return !LogHolds(log, record); });
  if (!missing.empty()) {
    std::ranges::sort(missing, RecordLess);
    log.log.MergeSorted(std::move(missing), RecordLess);
  }
  auto& queue = world.get_mut<z13::gameplay::ScheduledCommands>();
  for (const z13::gameplay::PlayerActionRecord& record : input.pending) {
    const bool queued =
        std::ranges::any_of(queue.records, [&record](const auto& entry) { return SameRecord(entry, record); });
    if (!queued && !LogHolds(log, record)) {
      QueueInOrder(queue, record);
    }
  }
  for (const auto& [player_id, through_tick] : input.confirmed.by_player) {
    ConfirmInputThrough(world, player_id, through_tick);
  }
}

std::expected<std::unique_ptr<fbs::net::CatchUpT>, std::string> MakeCatchUp(
    flecs::world world, const std::optional<InterestView>& view) {
  auto catch_up = std::make_unique<fbs::net::CatchUpT>();
  catch_up->server_tick = world.get<ft::SimulationClock>().tick;
  const auto in_view = [&view](const z13::gameplay::PlayerActionRecord& record) {
    return !view || view->players.contains(record.player_id);
  };
  const auto filtered = [&](const ft::WorldSnapshot& snapshot) {
    return view ? ToWire(FilterSnapshot(world, snapshot, *view)) : ToWire(snapshot);
  };

  const auto& history = world.get<ft::WorldSnapshotHistory>();
  if (history.history.Empty()) {
    const auto snapshot = ft::CaptureState(world);
    if (!snapshot) {
      return std::unexpected(snapshot.error());
    }
    catch_up->snapshot = filtered(*snapshot);
    catch_up->snapshot_tick = catch_up->server_tick;
  } else {
    const ft::TimestampedSnapshot& base =
        CatchUpBase(history, catch_up->server_tick, ft::DeferredRollbackTick(world), world.get<NetTuning>().max_late_ticks,
                    AllConfirmedThrough(world.get<z13::gameplay::ConfirmedInputTicks>()));
    catch_up->snapshot = filtered(base.snapshot);
    catch_up->snapshot_tick = base.tick;
    catch_up->actions = ToWire(Filtered(ActionsSince(world, base.tick), in_view));
  }
  catch_up->held_values = ToWire(Filtered(HeldValues(world, catch_up->snapshot_tick), in_view));
  catch_up->pending = ToWire(Filtered(world.get<z13::gameplay::ScheduledCommands>().records, in_view));
  auto confirmed = world.get<z13::gameplay::ConfirmedInputTicks>().by_player;
  if (view) {
    boost::container::erase_if(confirmed, [&view](const auto& entry) { return !view->players.contains(entry.first); });
  }
  catch_up->input_through = ToWire(confirmed);
  return catch_up;
}

std::expected<CatchUpPayload, std::string> DecodeCatchUp(const std::unique_ptr<fbs::net::CatchUpT>& catch_up) {
  if (!catch_up || !catch_up->snapshot) {
    return std::unexpected(std::string {"no snapshot"});
  }
  return CatchUpPayload {
      .server_tick = catch_up->server_tick,
      .snapshot_tick = catch_up->snapshot_tick,
      .snapshot = ft::FromFlatbuffer(*catch_up->snapshot),
      .actions = FromWire(catch_up->actions),
      .held_values = FromWire(catch_up->held_values),
      .pending = FromWire(catch_up->pending),
      .confirmed = FromWire(catch_up->input_through),
  };
}

bool IsPlausibleCatchUp(flecs::world world, uint64_t snapshot_tick, uint64_t target_tick) {
  return target_tick >= snapshot_tick &&
         target_tick - snapshot_tick <= world.get<z13::ActiveCoreSettings>().max_catch_up_ticks_per_frame;
}

// In the world's thread for now; see «Ресинк в одном потоке» in docs/client-server-plan.md.
void AdoptCatchUp(flecs::world world, CatchUpPayload payload, uint64_t target_tick) {
  auto& digests = world.get_mut<StateDigests>();
  std::vector<z13::gameplay::PlayerActionRecord> server_entries = std::move(payload.held_values);
  server_entries.insert(server_entries.end(), payload.actions.begin(), payload.actions.end());
  // Own input the catch-up lacks: sent after the ResyncRequest, or not sent yet. A tie keeps the
  // server's record first.
  std::vector<z13::gameplay::PlayerActionRecord> own_entries =
      digests.awaiting_resync.value_or(std::vector<z13::gameplay::PlayerActionRecord> {});
  std::ranges::copy(world.get<z13::gameplay::OutgoingCommands>().records, std::back_inserter(own_entries));
  std::ranges::sort(own_entries, RecordLess);
  std::vector<z13::gameplay::PlayerActionRecord> log_entries;
  log_entries.reserve(server_entries.size() + own_entries.size());
  std::ranges::merge(server_entries, own_entries, std::back_inserter(log_entries), RecordLess);
  auto& log = world.get_mut<z13::gameplay::PlayerActionLog>().log;
  log.RemoveIf([](const auto&) { return true; });
  if (!log_entries.empty()) {
    log.MergeSorted(std::move(log_entries), RecordLess);
  }

  world.get_mut<z13::gameplay::ScheduledCommands>().records = std::move(payload.pending);
  world.get_mut<z13::gameplay::ConfirmedInputTicks>() = std::move(payload.confirmed);

  auto& history = world.get_mut<ft::WorldSnapshotHistory>().history;
  history.RemoveIf([](const auto&) { return true; });
  history.Push({.tick = payload.snapshot_tick, .snapshot = std::move(payload.snapshot)});

  digests.local.clear();
  digests.received.clear();
  digests.awaiting_resync.reset();

  // A deferred rollback may reach past the adopted snapshot.
  world.get_mut<ft::RollbackRequest>() = {};
  log_debug("NetSession(client): adopting a catch-up from tick {} to {}", payload.snapshot_tick, target_tick);
  ft::RequestRollback(world, payload.snapshot_tick, target_tick);
}

}  // namespace z13::net
