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

#include "door_system.h"

#include <algorithm>
#include <memory>
#include <tuple>
#include <vector>

#include <flecs.h>

#include <lib_core/state/world_serializer.h>
#include <lib_core/state/world_state.h>
#include <lib_core/time/simulation_clock.h>
#include <lib_core/world/lifecycle.h>

#include <primitives/palette.h>
#include <rooms/portal_source.h>
#include <z13/components/block.h>
#include <z13/components/block_change_reader.h>
#include <z13/components/door.h>
#include <z13/components/interaction.h>
#include <z13/components/station_mode.h>
#include <z13/components/utilities.h>
#include <z13_settings/door_tuning.h>

namespace z13::station {

namespace {

using z13::building::primitives::BlockPalette;
using Methods = fbs::station::InteractionMethods;

bool IsDoorBlock(const Block& block, const BlockPalette& palette) {
  const auto primitive = palette.palette.Find(block.spec.type_id);
  return primitive && primitive->get().IsDoor();
}

// What the players did to one door this tick.
struct Contact {
  bool presence {};
  // Used an odd number of times: two players using it together cancel out.
  bool flipped {};
};

Contact ContactOf(const Interactions& interactions, flecs::entity_t door) {
  Contact contact;
  for (const Interaction& interaction : interactions.list) {
    if (interaction.target != door) {
      continue;
    }
    contact.presence = contact.presence || interaction.method == Methods::Presence;
    contact.flipped = contact.flipped != (interaction.method == Methods::Use);
  }
  return contact;
}

void MoveDoor(flecs::entity door, DoorState& state, const Interactions& interactions, const z13::DoorTuning& tuning) {
  const bool supplied = z13::utility::IsSupplied(door);
  const bool by_hand = !supplied || state.mode == DoorMode::kManual;
  if (state.mode == DoorMode::kLocked || state.mode == DoorMode::kWelded) {
    state.open_wanted = false;
    state.hold_ticks = 0;
  } else {
    const Contact contact = ContactOf(interactions, door.id());
    if (contact.flipped) {
      state.open_wanted = !state.open_wanted;
      state.hold_ticks = state.open_wanted ? static_cast<int>(tuning.close_delay_ticks) : 0;
    }
    if (state.mode == DoorMode::kAuto && !by_hand) {
      if (contact.presence) {
        state.open_wanted = true;
        state.hold_ticks = static_cast<int>(tuning.close_delay_ticks);
      } else if (state.open_wanted) {
        state.open_wanted = state.hold_ticks > 0;
        state.hold_ticks = std::max(state.hold_ticks - 1, 0);
      }
    }
  }
  const int full = static_cast<int>(by_hand ? tuning.manual_open_ticks : tuning.open_ticks);
  const int step = std::max(kDoorOpen / std::max(full, 1), 1);
  const int target = state.open_wanted ? kDoorOpen : 0;
  const int position = state.position;
  state.position = static_cast<DoorPosition>(target > position ? std::min(position + step, target)
                                                               : std::max(position - step, target));
  state.leaf_blocks = state.position < static_cast<int>(tuning.passable_position);
}

// The doors of the blocks that came this tick, or of all of them after a restore.
void AddStates(BlockChangeReader& reader, uint64_t restores, uint64_t tick, const BlockPalette& palette,
               const DoorsStartOpen& start, const flecs::world& world) {
  const BlockChangeBatch batch = reader.Read(restores, tick);
  const DoorState first = start.open ? DoorState {.position = kDoorOpen, .open_wanted = true, .leaf_blocks = false}
                                     : DoorState {};
  const auto add = [&](flecs::entity block, const Block& placed) {
    if (!block.has<DoorState>() && IsDoorBlock(placed, palette)) {
      block.set(first);
    }
  };
  if (batch.resync) {
    reader.Blocks().each(add);
    return;
  }
  for (const BlockChange& change : batch.changes) {
    if (change.kind == BlockChangeKind::kAdded && world.is_alive(change.entity)) {
      add(world.entity(change.entity), change.block);
    }
  }
}

void UpdatePortals(const flecs::query<const DoorState, const Block>& doors, const BlockPalette& palette,
                   DoorPortals& portals) {
  std::vector<DoorOpening> open;
  doors.each([&](const DoorState& state, const Block& block) {
    const auto primitive = palette.palette.Find(block.spec.type_id);
    if (state.position == 0 || !primitive) {
      return;
    }
    for (const rooms::PortalSource& source : rooms::PortalsOf(block, primitive->get(), palette.palette)) {
      open.push_back({.min = source.opening.min, .extent = source.opening.extent});
    }
  });
  std::ranges::sort(open, [](const DoorOpening& a, const DoorOpening& b) {
    return std::tie(a.min.x(), a.min.y(), a.min.z(), a.extent.x(), a.extent.y(), a.extent.z()) <
           std::tie(b.min.x(), b.min.y(), b.min.z(), b.extent.x(), b.extent.y(), b.extent.z());
  });
  if (open != portals.open) {
    portals.open = std::move(open);
  }
}

void RegisterComponents(flecs::world world) {
  z13::flecs_tools::RegisterComponents<DoorState, DoorPortals, DoorsStartOpen>(world);
}

void RegisterPipeline(flecs::world world) {
  // Doors read what the players did this tick and what the power gave.
  world.component<z13::utility::UtilityConsumersPhase>().add(flecs::Phase).depends_on<InteractionPhase>();
}

void RegisterSystems(flecs::world world) {
  world.set(DoorPortals {});
  world.set(DoorsStartOpen {});

  const auto reader = std::make_shared<BlockChangeReader>(world);
  world.system<const BlockPalette, const DoorsStartOpen, const z13::flecs_tools::WorldRestores,
               const z13::flecs_tools::SimulationClock>("DoorSystem::AddStates")
      .kind<z13::utility::UtilityConsumersPhase>()
      .with<StationMode>()
      .read<Block>()
      .read<BlockChange>()
      .write<DoorState>()
      .each([reader](flecs::iter& it, size_t, const BlockPalette& palette, const DoorsStartOpen& start,
                     const z13::flecs_tools::WorldRestores& restores, const z13::flecs_tools::SimulationClock& clock) {
        AddStates(*reader, restores.count, clock.tick, palette, start, it.world());
      });

  world.system<DoorState, const Interactions, const z13::DoorTuning>("DoorSystem::Move")
      .kind<z13::utility::UtilityConsumersPhase>()
      .with<StationMode>()
      .read<z13::utility::UtilitySupply>()
      .each(MoveDoor);

  const auto doors = world.query<const DoorState, const Block>();
  world.system<DoorPortals, const BlockPalette>("DoorSystem::UpdatePortals")
      .kind<z13::utility::UtilityConsumersPhase>()
      .with<StationMode>()
      .read<DoorState>()
      .read<Block>()
      .each([doors](DoorPortals& portals, const BlockPalette& palette) { UpdatePortals(doors, palette, portals); });
}

}  // namespace

void DoorSystem::Register(flecs::world& world) {
  z13::OnRegisterComponents(world, RegisterComponents);
  z13::OnInitPhases(world, RegisterPipeline);
  z13::OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::station
