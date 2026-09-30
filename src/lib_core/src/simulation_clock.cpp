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
#include <vector>

#include <lib_core/config.h>
#include <lib_core/flecs_utils.h>
#include <lib_core/world_state.h>

namespace z13::flecs_tools {

namespace {

void IncrementTick(SimulationClock& clock) {
  ++clock.tick;
}

std::optional<uint64_t> SecondsToTicks(flecs::world world, std::optional<double> seconds) {
  const std::optional<uint64_t> ticks_per_second = TicksPerSecond(world);
  if (!ticks_per_second || !seconds) {
    return std::nullopt;
  }
  return static_cast<uint64_t>(std::llround(*seconds * static_cast<double>(*ticks_per_second)));
}

}  // namespace

void RegisterSimulationClock(flecs::world& world) {
  RegisterComponents<SimulationClock, SimulationFrozen>(world);
  world.set<SimulationClock>({});
  world.component<PresentationPhase>();

  world.component<SimulationTickPhase>().add(flecs::Phase);
  world.get_alive(flecs::PreFrame).add(flecs::Phase).add<PresentationPhase>().depends_on<SimulationTickPhase>();

  world.system<SimulationClock>("SimulationClock::IncrementTick")
      .kind<SimulationTickPhase>()
      .each(IncrementTick);
}

void ApplySimulationFreeze(flecs::world& world) {
  const bool frozen = world.has<SimulationFrozen>();
  // All simulation phases toggle together.
  if (world.component<SimulationTickPhase>().enabled() != frozen) {
    return;
  }
  // Optional Disabled term: also match already-frozen phases.
  std::vector<flecs::entity> phases;
  world.query_builder()
      .with(flecs::Phase)
      .without<PresentationPhase>()
      .with(flecs::Disabled).optional()
      .build()
      .each([&phases](flecs::entity phase) { phases.push_back(phase); });
  for (flecs::entity phase : phases) {
    if (phase == flecs::OnStart) {
      continue;
    }
    if (frozen) {
      phase.disable();
    } else {
      phase.enable();
    }
  }
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

std::optional<uint64_t> SnapshotIntervalTicks(flecs::world world) {
  return SecondsToTicks(world, SnapshotIntervalSeconds(world));
}

std::optional<uint64_t> SnapshotRetentionTicks(flecs::world world) {
  return SecondsToTicks(world, SnapshotRetentionSeconds(world));
}

}  // namespace z13::flecs_tools
