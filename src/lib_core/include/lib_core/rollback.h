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

#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include <flecs.h>

namespace z13::flecs_tools {

// Requests coalesce: one rollback, to the earliest tick asked for, up to the latest target.
struct RollbackRequest {
  using Singleton = void;
  using SessionScoped = void;
  std::optional<uint64_t> to_tick;
  uint64_t target_tick {};
  std::optional<uint64_t> deferred_to_tick;
  uint64_t deferred_since_tick {};
  uint32_t deferred_count {};
};

// Set while the world re-simulates ticks it has already run; systems that must not fire
// twice (live input, recorders) filter it out.
struct ReplayInProgress {
  using Singleton = void;
  uint64_t from_tick {};
  uint64_t target_tick {};
};

// Set when no retained snapshot was old enough; whoever asked for the rollback removes it.
struct RollbackFailed {
  using Singleton = void;
  std::string reason;
};

// Depth is target_tick minus the restored snapshot's tick, i.e. the ticks re-simulated.
struct RollbackMetrics {
  using Singleton = void;
  uint64_t rollbacks {};
  uint64_t last_depth_ticks {};
  uint64_t max_depth_ticks {};
};

// Positive: extra live ticks next frame; negative: frames to skip.
struct ClockAdjustRequest {
  using Singleton = void;
  int64_t ticks {};
};

enum class FrameKind {
  kLive,
  // Another frame follows right away: a wall-clock backlog or extra clock ticks.
  kCatchUp,
  // Re-simulates a tick already shown; never worth presenting.
  kReplay,
};

using FrameStartCallback = std::function<void(flecs::world&, FrameKind)>;

// Called by TickWorld between frames, right before each progress(), so the callback may
// enable or disable phases for the coming frame.
void OnFrameStart(flecs::world& world, FrameStartCallback callback);

// Called by RegisterStateMeta.
void RegisterRollback(flecs::world& world);

// Restores the newest snapshot at or before `to_tick` and re-simulates up to
// `target_tick`. Safe to call from a system -- the restore lands between frames.
void RequestRollback(flecs::world& world, uint64_t to_tick, uint64_t target_tick);

// RequestRollback up to the present, batched with other deferred requests.
void DeferRollback(flecs::world& world, uint64_t to_tick);

// State after this tick is about to be re-simulated.
std::optional<uint64_t> DeferredRollbackTick(flecs::world world);

void RequestClockAdjust(flecs::world& world, int64_t ticks);

bool IsCatchingUp(flecs::world world);

// The one place a world is advanced: an ordinary frame, plus the frames a rollback
// requested during it needs, adjusted by a ClockAdjustRequest. Nothing else may call
// progress() -- the restore has to land between frames, or flecs skips the rest of that
// frame's pipeline. `kind` is the caller's view of the frame (kCatchUp when it is behind the
// wall clock); extra clock ticks and replays override it.
void TickWorld(flecs::world& world, float delta_time, FrameKind kind = FrameKind::kLive);

}  // namespace z13::flecs_tools
