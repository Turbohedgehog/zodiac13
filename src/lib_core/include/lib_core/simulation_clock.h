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

#include <flecs.h>

namespace z13::flecs_tools {

// Monotonic simulation tick, world state so every snapshot records which tick it was
// taken at. The tick's actual duration is Config::GetFPS() (constant for the life of a
// run) -- not duplicated here; read it from there when needed.
struct SimulationClock {
  using State = void;
  using Singleton = void;
  uint64_t tick {};
};

// Registers SimulationClock and the system that increments `tick` once per
// world.progress() call, in flecs::PreFrame so every other system of that frame sees
// the tick it's currently on. Called by RegisterStateMeta.
void RegisterSimulationClock(flecs::world& world);

// Config values read through CoreComponent so callers don't each duplicate the lookup.
// Nullopt in worlds with no CoreComponent (e.g. bare lib_core tests).
std::optional<uint64_t> TicksPerSecond(flecs::world world);  // Config::GetFPS(), rounded
std::optional<double> SnapshotIntervalSeconds(flecs::world world);
std::optional<double> SnapshotRetentionSeconds(flecs::world world);

}  // namespace z13::flecs_tools
