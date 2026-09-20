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

#include <lib_core/world_snapshot_history.h>

#include <cmath>

#include <lib_core/simulation_clock.h>
#include <lib_core/world_state.h>

namespace z13::flecs_tools {

namespace {

uint64_t RoundTicks(double seconds, uint64_t ticks_per_second) {
  return static_cast<uint64_t>(std::llround(seconds * static_cast<double>(ticks_per_second)));
}

void CaptureSnapshot(flecs::world world, const SimulationClock& clock, WorldSnapshotHistory& history) {
  const std::optional<uint64_t> ticks_per_second = TicksPerSecond(world);
  const std::optional<double> interval_seconds = SnapshotIntervalSeconds(world);
  const std::optional<double> retention_seconds = SnapshotRetentionSeconds(world);
  if (!ticks_per_second || !interval_seconds || !retention_seconds) {
    return;
  }

  const uint64_t interval_ticks = RoundTicks(*interval_seconds, *ticks_per_second);
  if (interval_ticks == 0 || clock.tick % interval_ticks != 0) {
    return;
  }

  TimestampedSnapshot entry;
  entry.tick = clock.tick;
  entry.snapshot = CaptureState(world);
  history.history.Push(std::move(entry));

  // From ticks_per_second directly, not interval_ticks * retention_seconds -- those
  // agree only when interval_seconds == 1.
  const uint64_t retention_ticks = RoundTicks(*retention_seconds, *ticks_per_second);
  const uint64_t current_tick = clock.tick;
  auto is_too_old = [current_tick, retention_ticks](const TimestampedSnapshot& old_entry) {
    return (current_tick - old_entry.tick) > retention_ticks;
  };
  history.history.PruneOlderThan(is_too_old);
}

}  // namespace

void RegisterWorldSnapshotHistory(flecs::world& world) {
  RegisterComponent<WorldSnapshotHistory>(world);
  world.set<WorldSnapshotHistory>({});

  // Lambda wrapper: passing CaptureSnapshot directly crashes this MSVC's .each() with
  // an ICE (flecs::world as the first param -- same workaround as input_publisher.cpp).
  world.system<const SimulationClock, WorldSnapshotHistory>("WorldSnapshotHistory::CaptureSnapshot")
      .kind(flecs::PostFrame)
      .each([](flecs::iter& it, size_t, const SimulationClock& clock, WorldSnapshotHistory& history) {
        CaptureSnapshot(it.world(), clock, history);
      });
}

}  // namespace z13::flecs_tools
