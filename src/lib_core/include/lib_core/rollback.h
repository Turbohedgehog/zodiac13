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
#include <optional>
#include <string>

#include <flecs.h>

namespace z13::flecs_tools {

// Safety bound, not a scheduler: a corrupt target tick spreads over calls instead of
// hanging the loop.
inline constexpr uint64_t kMaxCatchUpTicksPerFrame {1024};

// Requests coalesce: one rollback, to the earliest tick asked for, up to the latest target.
struct RollbackRequest {
  using Singleton = void;
  std::optional<uint64_t> to_tick;
  uint64_t target_tick {};
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

// Called by RegisterStateMeta.
void RegisterRollback(flecs::world& world);

// Restores the newest snapshot at or before `to_tick` and re-simulates up to
// `target_tick`. Safe to call from a system -- the restore lands between frames.
void RequestRollback(flecs::world& world, uint64_t to_tick, uint64_t target_tick);

bool IsCatchingUp(flecs::world world);

// The one place a world is advanced: an ordinary frame, plus the frames a rollback
// requested during it needs. Nothing else may call progress() -- the restore has to land
// between frames, or flecs skips the rest of that frame's pipeline.
void TickWorld(flecs::world& world, float delta_time);

}  // namespace z13::flecs_tools
