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

#include <lib_core/rollback.h>

#include <algorithm>
#include <format>
#include <utility>

#include <lib_core/simulation_clock.h>
#include <lib_core/world_serializer.h>
#include <lib_core/world_snapshot_history.h>
#include <lib_core/world_state.h>

namespace z13::flecs_tools {

namespace {

// Not mid-replay: the replay's own target would be lost.
void PromoteDeferredRollback(flecs::world& world, RollbackRequest& request) {
  if (!request.deferred_to_tick || world.has<ReplayInProgress>()) {
    return;
  }
  const uint64_t now = world.get<SimulationClock>().tick;
  const bool due = request.to_tick || request.deferred_count >= kMaxDeferredRollbacks ||
      now >= request.deferred_since_tick + kMaxRollbackDelayTicks;
  if (!due) {
    return;
  }
  request.to_tick = std::min(request.to_tick.value_or(*request.deferred_to_tick), *request.deferred_to_tick);
  request.target_tick = std::max(request.target_tick, now);
  request.deferred_to_tick.reset();
  request.deferred_count = 0;
}

void ApplyPendingRollback(flecs::world& world) {
  auto& request = world.get_mut<RollbackRequest>();
  PromoteDeferredRollback(world, request);
  if (!request.to_tick) {
    return;
  }
  const uint64_t to_tick = *request.to_tick;
  const uint64_t target_tick = request.target_tick;
  request.to_tick.reset();
  request.target_tick = 0;

  auto& history = world.get_mut<WorldSnapshotHistory>();
  const auto& entries = history.history.Entries();
  auto restore_point = entries.end();
  for (auto entry = entries.begin(); entry != entries.end(); ++entry) {
    // Scanned, not assumed last: a join pushes its snapshot out of tick order. Ties go to
    // the newest push, which is that join's snapshot rather than a local one of the same tick.
    if (entry->tick <= to_tick && (restore_point == entries.end() || entry->tick >= restore_point->tick)) {
      restore_point = entry;
    }
  }
  if (restore_point == entries.end()) {
    world.set<RollbackFailed>(
        {std::format("rollback to tick {} has no retained snapshot that old", to_tick)});
    return;
  }

  const uint64_t from_tick = restore_point->tick;
  if (const auto restored = RestoreWorld(world, restore_point->snapshot); !restored) {
    world.set<RollbackFailed>({restored.error()});
    return;
  }
  // Everything newer is a future the replay is about to derive again; PostFrame re-captures it.
  history.history.RemoveIf([from_tick](const TimestampedSnapshot& entry) { return entry.tick > from_tick; });

  auto& metrics = world.get_mut<RollbackMetrics>();
  const uint64_t depth = target_tick > from_tick ? target_tick - from_tick : 0;
  ++metrics.rollbacks;
  metrics.last_depth_ticks = depth;
  metrics.max_depth_ticks = std::max(metrics.max_depth_ticks, depth);

  // The clock is state, so the restore moved it back already; the next frame's increment
  // lands on the first replayed tick. Nothing to replay when the snapshot is the target.
  if (from_tick < target_tick) {
    world.set<ReplayInProgress>({.from_tick = from_tick, .target_tick = target_tick});
  }
}

void FinishReplay(flecs::iter& it, size_t, const SimulationClock& clock, const ReplayInProgress& replay) {
  if (clock.tick >= replay.target_tick) {
    it.world().remove<ReplayInProgress>();
  }
}

void AdvanceTick(flecs::world& world, float delta_time) {
  for (uint64_t frame = 0; frame <= kMaxCatchUpTicksPerFrame; ++frame) {
    world.progress(delta_time);
    ApplyPendingRollback(world);
    if (!IsCatchingUp(world)) {
      return;
    }
  }
}

}  // namespace

void RegisterRollback(flecs::world& world) {
  RegisterComponents<RollbackRequest, ReplayInProgress, RollbackFailed, RollbackMetrics, ClockAdjustRequest>(world);
  world.set<RollbackRequest>({});
  world.set<ClockAdjustRequest>({});
  world.set<RollbackMetrics>({});

  world.system<const SimulationClock, const ReplayInProgress>("Rollback::FinishReplay")
      .kind(flecs::PostFrame)
      .each(FinishReplay);
}

void RequestRollback(flecs::world& world, uint64_t to_tick, uint64_t target_tick) {
  auto& request = world.get_mut<RollbackRequest>();
  request.to_tick = request.to_tick ? std::min(*request.to_tick, to_tick) : to_tick;
  request.target_tick = std::max(request.target_tick, target_tick);
}

void DeferRollback(flecs::world& world, uint64_t to_tick) {
  auto& request = world.get_mut<RollbackRequest>();
  if (!request.deferred_to_tick) {
    request.deferred_since_tick = world.get<SimulationClock>().tick;
  }
  request.deferred_to_tick = std::min(request.deferred_to_tick.value_or(to_tick), to_tick);
  ++request.deferred_count;
}

std::optional<uint64_t> DeferredRollbackTick(flecs::world world) {
  return world.has<RollbackRequest>() ? world.get<RollbackRequest>().deferred_to_tick : std::nullopt;
}

void RequestClockAdjust(flecs::world& world, int64_t ticks) {
  world.get_mut<ClockAdjustRequest>().ticks += ticks;
}

bool IsCatchingUp(flecs::world world) {
  return world.has<ReplayInProgress>() ||
         (world.has<RollbackRequest>() && world.get<RollbackRequest>().to_tick.has_value());
}

void TickWorld(flecs::world& world, float delta_time) {
  const int64_t adjust = world.has<ClockAdjustRequest>() ? std::exchange(world.get_mut<ClockAdjustRequest>().ticks, 0) : 0;
  if (adjust < 0) {
    world.get_mut<ClockAdjustRequest>().ticks += adjust + 1;
    return;
  }
  const int64_t extra_ticks = std::min<int64_t>(adjust, kMaxCatchUpTicksPerFrame);
  for (int64_t tick = 0; tick <= extra_ticks; ++tick) {
    AdvanceTick(world, delta_time);
  }
}

}  // namespace z13::flecs_tools
