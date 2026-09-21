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

#include <lib_core/simulation_clock.h>

#include <cmath>

#include <lib_core/config.h>
#include <lib_core/flecs_utils.h>
#include <lib_core/world_state.h>

namespace z13::flecs_tools {

namespace {

void IncrementTick(SimulationClock& clock) {
  ++clock.tick;
}

}  // namespace

void RegisterSimulationClock(flecs::world& world) {
  RegisterComponent<SimulationClock>(world);
  world.set<SimulationClock>({});

  world.system<SimulationClock>("SimulationClock::IncrementTick")
      .kind(flecs::PreFrame)
      .each(IncrementTick);
}

std::optional<uint64_t> TicksPerSecond(flecs::world world) {
  const auto config = GetCoreConfig(world);
  if (!config) {
    return std::nullopt;
  }
  return static_cast<uint64_t>(std::llround(config->get().GetFPS()));
}

std::optional<double> SnapshotIntervalSeconds(flecs::world world) {
  const auto config = GetCoreConfig(world);
  return config ? std::optional(config->get().GetSnapshotIntervalSeconds()) : std::nullopt;
}

std::optional<double> SnapshotRetentionSeconds(flecs::world world) {
  const auto config = GetCoreConfig(world);
  return config ? std::optional(config->get().GetSnapshotRetentionSeconds()) : std::nullopt;
}

}  // namespace z13::flecs_tools
