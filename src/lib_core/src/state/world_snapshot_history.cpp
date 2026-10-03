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

#include <lib_core/state/world_snapshot_history.h>

#include <lib_core/state/world_state.h>
#include <lib_core/time/simulation_clock.h>
#include <lib_core/utils/log.h>

namespace z13::flecs_tools {

namespace {

void CaptureSnapshot(
    flecs::world world, const SimulationClock& clock, const SnapshotCaptureRate& rate, WorldSnapshotHistory& history) {
  const std::optional<uint64_t> interval_ticks = SnapshotIntervalTicks(world);
  const std::optional<uint64_t> retention_ticks = SnapshotRetentionTicks(world);
  if (!interval_ticks || !retention_ticks || rate.per_interval == 0) {
    return;
  }
  const uint64_t capture_ticks = *interval_ticks / rate.per_interval;
  if (capture_ticks == 0 || clock.tick % capture_ticks != 0) {
    return;
  }

  auto snapshot = CaptureState(world);
  if (!snapshot) {
    if (history.capture_error != snapshot.error()) {
      log_error("z13::WorldSnapshotHistory: capture at tick {} failed: {}", clock.tick, snapshot.error());
      history.capture_error = snapshot.error();
    }
    return;
  }
  history.capture_error.reset();
  history.history.Push({.tick = clock.tick, .snapshot = std::move(*snapshot)});

  const uint64_t current_tick = clock.tick;
  auto is_too_old = [current_tick, retention_ticks = *retention_ticks](const TimestampedSnapshot& old_entry) {
    return (current_tick - old_entry.tick) > retention_ticks;
  };
  history.history.PruneOlderThan(is_too_old);
}

}  // namespace

void RegisterWorldSnapshotHistory(flecs::world& world) {
  RegisterComponents<WorldSnapshotHistory, SnapshotCaptureRate>(world);
  world.set<WorldSnapshotHistory>({});
  world.set<SnapshotCaptureRate>({});

  // Lambda wrapper: passing CaptureSnapshot directly crashes this MSVC's .each() with
  // an ICE (flecs::world as the first param -- same workaround as input_publisher.cpp).
  world.system<const SimulationClock, const SnapshotCaptureRate, WorldSnapshotHistory>(
      "WorldSnapshotHistory::CaptureSnapshot")
      .kind(flecs::PostFrame)
      .each([](flecs::iter& it, size_t, const SimulationClock& clock, const SnapshotCaptureRate& rate,
               WorldSnapshotHistory& history) { CaptureSnapshot(it.world(), clock, rate, history); });
}

}  // namespace z13::flecs_tools
