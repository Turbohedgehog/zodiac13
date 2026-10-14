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

#include "switch_system.h"

#include <unordered_map>

#include <flecs.h>

#include <lib_core/state/world_state.h>
#include <lib_core/world/lifecycle.h>

#include <z13/components/block.h>
#include <z13/components/interaction.h>
#include <z13/components/switch.h>
#include <primitives/palette.h>

namespace z13::station {

namespace {

using z13::building::primitives::BlockPalette;
using z13::building::primitives::PrimitiveFlags;

bool IsSwitch(flecs::entity block, const BlockPalette& palette) {
  const auto primitive = palette.palette.Find(block.get<Block>().spec.type_id);
  return primitive && primitive->get().Has(PrimitiveFlags::Switch);
}

// Players using one switch on the same tick each flip it.
void ToggleSwitches(flecs::iter& it, size_t, const Interactions& interactions, const BlockPalette& palette) {
  const flecs::world world = it.world();
  std::unordered_map<flecs::entity_t, bool> flipped;
  for (const Interaction& interaction : interactions.list) {
    if (interaction.method == fbs::station::InteractionMethods::Use && world.is_alive(interaction.target) &&
        IsSwitch(world.entity(interaction.target), palette)) {
      flipped[interaction.target] = !flipped[interaction.target];
    }
  }
  for (const auto& [target, flip] : flipped) {
    const flecs::entity block = world.entity(target);
    if (flip) {
      block.set(SwitchState {.on = !(block.has<SwitchState>() && block.get<SwitchState>().on)});
    }
  }
}

void RegisterComponents(flecs::world world) {
  z13::flecs_tools::RegisterComponent<SwitchState>(world);
}

void RegisterPipeline(flecs::world world) {
  world.component<SwitchPhase>().add(flecs::Phase).depends_on<InteractionPhase>();
}

void RegisterSystems(flecs::world world) {
  world.system<const Interactions, const BlockPalette>("SwitchSystem::Toggle")
      .kind<SwitchPhase>()
      .read<Block>()
      .read<SwitchState>()
      .each(ToggleSwitches);
}

}  // namespace

void SwitchSystem::Register(flecs::world& world) {
  OnRegisterComponents(world, RegisterComponents);
  OnInitPhases(world, RegisterPipeline);
  OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::station
