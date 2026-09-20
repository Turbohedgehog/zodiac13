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

#include <flecs.h>

#include <lib_core/bounded_history.h>
#include <lib_core/world_serializer.h>

namespace z13::flecs_tools {

struct TimestampedSnapshot {
  uint64_t tick {};
  WorldSnapshot snapshot;
};

// A rolling window of recent full-scene snapshots; runtime only, never itself part of
// the state it captures (like PendingWorldState).
struct WorldSnapshotHistory {
  using Singleton = void;
  BoundedHistory<TimestampedSnapshot> history;
};

// Captures a snapshot every Config::GetSnapshotIntervalSeconds(), keeping
// Config::GetSnapshotRetentionSeconds() of history. A no-op without CoreComponent (e.g.
// bare lib_core tests), since Config is only reachable through it.
void RegisterWorldSnapshotHistory(flecs::world& world);

}  // namespace z13::flecs_tools
