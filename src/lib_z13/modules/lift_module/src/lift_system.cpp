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

#include "lift_system.h"

#include <algorithm>
#include <cstdint>

#include <flecs.h>

#include <lib_core/state/world_state.h>
#include <lib_core/time/simulation_clock.h>
#include <lib_core/world/lifecycle.h>

#include <z13/components/gameplay/kinematic_body.h>
#include <z13/components/station/block.h>
#include <z13/components/station/interaction.h>
#include <z13/components/station/lift.h>

namespace z13::lift_module {

namespace {

using z13::kinematic::KinematicBody;
using z13::station::Lift;

bool IsIdle(const KinematicBody& cabin, uint64_t tick) {
  return tick >= cabin.start_tick + cabin.duration_ticks;
}

bool ButtonUsed(const flecs::world& world, const Lift& lift, const z13::station::Interactions& interactions) {
  return std::ranges::any_of(interactions.list, [&](const z13::station::Interaction& interaction) {
    if (interaction.method != fbs::station::InteractionMethods::Use || !world.is_alive(interaction.target)) {
      return false;
    }
    const auto* block = world.entity(interaction.target).try_get<z13::station::Block>();
    return block != nullptr && block->grid == z13::station::kMainGrid &&
           (block->cell == lift.lower_button || block->cell == lift.upper_button);
  });
}

void SendCabin(flecs::iter& it, size_t, const Lift& lift, KinematicBody& cabin,
               const z13::station::Interactions& interactions, const z13::flecs_tools::SimulationClock& clock) {
  if (!IsIdle(cabin, clock.tick) || !ButtonUsed(it.world(), lift, interactions)) {
    return;
  }
  const bool at_upper = cabin.to == lift.upper;
  cabin = {.spec = cabin.spec,
           .from = cabin.to,
           .to = at_upper ? lift.lower : lift.upper,
           .start_tick = clock.tick,
           .duration_ticks = lift.trip_ticks};
}

void RegisterComponents(flecs::world world) {
  z13::flecs_tools::RegisterComponent<Lift>(world);
}

void RegisterPipeline(flecs::world world) {
  world.component<LiftPhase>().add(flecs::Phase).depends_on<z13::station::InteractionPhase>();
}

void RegisterSystems(flecs::world world) {
  world.system<const Lift, KinematicBody, const z13::station::Interactions, const z13::flecs_tools::SimulationClock>(
           "LiftSystem::SendCabin")
      .kind<LiftPhase>()
      .read<z13::station::Block>()
      .each(SendCabin);
}

}  // namespace

void LiftSystem::Register(flecs::world& world) {
  OnRegisterComponents(world, RegisterComponents);
  OnInitPhases(world, RegisterPipeline);
  OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::lift_module
