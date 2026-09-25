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

#include <lib_core/simulation_clock.h>
#include <lib_core/world_serializer.h>
#include <lib_core/world_snapshot_history.h>
#include <lib_core/world_state.h>

namespace z13::flecs_tools {

namespace {

void ApplyPendingRollback(flecs::world& world) {
  auto& request = world.get_mut<RollbackRequest>();
  if (!request.to_tick) {
    return;
  }
  const uint64_t to_tick = *request.to_tick;
  const uint64_t target_tick = request.target_tick;
  request = {};

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
  for (const auto& entity : restore_point->snapshot.entities) {
    if (entity.name.find("net") != std::string::npos || entity.name.find("Connection") != std::string::npos) {
    }
  }
  if (const auto restored = RestoreWorld(world, restore_point->snapshot); !restored) {
    world.set<RollbackFailed>({restored.error()});
    return;
  }
  // Everything newer is a future the replay is about to derive again; PostFrame re-captures it.
  history.history.RemoveIf([from_tick](const TimestampedSnapshot& entry) { return entry.tick > from_tick; });

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

}  // namespace

void RegisterRollback(flecs::world& world) {
  RegisterComponents<RollbackRequest, ReplayInProgress, RollbackFailed>(world);
  world.set<RollbackRequest>({});

  world.system<const SimulationClock, const ReplayInProgress>("Rollback::FinishReplay")
      .kind(flecs::PostFrame)
      .each(FinishReplay);
}

void RequestRollback(flecs::world& world, uint64_t to_tick, uint64_t target_tick) {
  auto& request = world.get_mut<RollbackRequest>();
  request.to_tick = request.to_tick ? std::min(*request.to_tick, to_tick) : to_tick;
  request.target_tick = std::max(request.target_tick, target_tick);
}

bool IsCatchingUp(flecs::world world) {
  return world.has<ReplayInProgress>() ||
         (world.has<RollbackRequest>() && world.get<RollbackRequest>().to_tick.has_value());
}

void TickWorld(flecs::world& world, float delta_time) {
  for (uint64_t frame = 0; frame <= kMaxCatchUpTicksPerFrame; ++frame) {
    world.progress(delta_time);
    ApplyPendingRollback(world);
    if (!IsCatchingUp(world)) {
      return;
    }
  }
}

}  // namespace z13::flecs_tools
