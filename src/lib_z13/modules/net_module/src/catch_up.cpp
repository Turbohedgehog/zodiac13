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
#include <z13_settings/net_tuning.h>

#include <net_module/state_digest.h>

#include "scheduled_commands.h"
#include "wire_records.h"

namespace z13::net {

namespace {

namespace ft = z13::flecs_tools;

// PlayerActionLog is sorted by tick.
std::vector<z13::gameplay::PlayerActionRecord> ActionsSince(flecs::world world, uint64_t since_tick) {
  const auto& entries = world.get<z13::gameplay::PlayerActionLog>().log.Entries();
  const auto first = std::ranges::upper_bound(entries, since_tick, {}, &z13::gameplay::PlayerActionRecord::tick);
  return {first, entries.end()};
}

// As of snapshot_tick, not now: the joiner replays `actions` from there.
std::vector<z13::gameplay::PlayerActionRecord> HeldValues(flecs::world world, uint64_t snapshot_tick) {
  const auto& entries = world.get<z13::gameplay::PlayerActionLog>().log.Entries();
  const auto up_to_snapshot = std::ranges::subrange(
      entries.begin(), std::ranges::upper_bound(entries, snapshot_tick, {}, &z13::gameplay::PlayerActionRecord::tick));
  boost::container::flat_map<std::pair<uint32_t, z13::input::ActionInfo::IdType>, float> values;
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

// The newest snapshot older than any command the server may still accept or has yet to
// replay, so the client can roll back for one; else the oldest, which the server can't go
// past either.
const ft::TimestampedSnapshot& CatchUpBase(
    const ft::WorldSnapshotHistory& history, uint64_t now, std::optional<uint64_t> deferred_rollback_tick,
    uint64_t max_late_ticks) {
  const auto& entries = history.history.Entries();
  const auto settled = std::ranges::find_last_if(entries, [&](const ft::TimestampedSnapshot& entry) {
    return entry.tick + max_late_ticks < now && entry.tick <= deferred_rollback_tick.value_or(entry.tick);
  });
  return settled.empty() ? entries.front() : settled.front();
}

}  // namespace

std::expected<std::unique_ptr<fbs::net::CatchUpT>, std::string> MakeCatchUp(flecs::world world) {
  auto catch_up = std::make_unique<fbs::net::CatchUpT>();
  catch_up->server_tick = world.get<ft::SimulationClock>().tick;

  // Reuses WorldSnapshotHistory's cache instead of a fresh CaptureState() per join (too
  // expensive); the action batch lets the client catch up to server_tick by replaying it.
  const auto& history = world.get<ft::WorldSnapshotHistory>();
  if (history.history.Empty()) {
    // Nothing cached yet -- fall back to a fresh capture; nothing to replay either.
    const auto snapshot = ft::CaptureState(world);
    if (!snapshot) {
      return std::unexpected(snapshot.error());
    }
    catch_up->snapshot = ToWire(*snapshot);
    catch_up->snapshot_tick = catch_up->server_tick;
  } else {
    const ft::TimestampedSnapshot& base = CatchUpBase(
        history, catch_up->server_tick, ft::DeferredRollbackTick(world), world.get<NetTuning>().max_late_ticks);
    catch_up->snapshot = ToWire(base.snapshot);
    catch_up->snapshot_tick = base.tick;
    catch_up->actions = ToWire(ActionsSince(world, base.tick));
  }
  catch_up->held_values = ToWire(HeldValues(world, catch_up->snapshot_tick));
  catch_up->pending = ToWire(world.get<z13::gameplay::ScheduledCommands>().records);
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
  };
}

bool IsPlausibleCatchUp(flecs::world world, uint64_t snapshot_tick, uint64_t target_tick) {
  return target_tick >= snapshot_tick &&
         target_tick - snapshot_tick <= world.get<z13::ActiveCoreSettings>().max_catch_up_ticks_per_frame;
}

void AdoptCatchUp(flecs::world world, CatchUpPayload payload, uint64_t target_tick) {
  std::vector<z13::gameplay::PlayerActionRecord> log_entries = std::move(payload.held_values);
  log_entries.insert(log_entries.end(), payload.actions.begin(), payload.actions.end());
  auto& log = world.get_mut<z13::gameplay::PlayerActionLog>().log;
  log.RemoveIf([](const auto&) { return true; });
  if (!log_entries.empty()) {
    log.MergeSorted(std::move(log_entries), RecordLess);
  }

  world.get_mut<z13::gameplay::ScheduledCommands>().records = std::move(payload.pending);

  auto& history = world.get_mut<ft::WorldSnapshotHistory>().history;
  history.RemoveIf([](const auto&) { return true; });
  history.Push({.tick = payload.snapshot_tick, .snapshot = std::move(payload.snapshot)});

  auto& digests = world.get_mut<StateDigests>();
  digests.local.clear();
  digests.received.clear();
  digests.awaiting_resync = false;

  // A deferred rollback may reach past the adopted snapshot.
  world.get_mut<ft::RollbackRequest>() = {};
  ft::RequestRollback(world, payload.snapshot_tick, target_tick);
}

}  // namespace z13::net
