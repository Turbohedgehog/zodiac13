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

#include "lamp_system.h"

#include <algorithm>
#include <utility>
#include <vector>

#include <flecs.h>

#include <lib_core/state/world_state.h>
#include <lib_core/world/lifecycle.h>

#include <primitives/palette.h>
#include <z13/components/station/block.h>
#include <z13/components/utility/lamps.h>
#include <z13/components/utility/power.h>
#include <z13/components/station/station_mode.h>
#include <z13/components/utility/utilities.h>

namespace z13::power_module {

namespace {

using z13::building::primitives::BlockPalette;
using z13::station::Block;
using z13::utility::DimmedLamp;
using z13::utility::DimmedLamps;
using z13::utility::LampsFollowPower;
using z13::utility::PowerStatus;
using z13::utility::UtilitySupply;

using LampQuery = flecs::query<const Block, const UtilitySupply>;

bool HasPower(const PowerStatus& status) {
  return std::ranges::any_of(status.networks, [](const z13::utility::PowerNetworkEntry& entry) {
    return entry.status.produced > 0 || entry.status.stored > 0;
  });
}

bool IsLamp(const Block& block, const BlockPalette& palette) {
  const auto primitive = palette.palette.Find(block.spec.type_id);
  return primitive && primitive->get().light;
}

void Update(DimmedLamps& dimmed, LampsFollowPower& follow, const PowerStatus& status, const BlockPalette& palette,
            const LampQuery& lamps) {
  follow.on = follow.on || HasPower(status);
  std::vector<DimmedLamp> list;
  if (follow.on) {
    lamps.each([&](const Block& block, const UtilitySupply& supply) {
      if (!supply.satisfied && IsLamp(block, palette)) {
        list.push_back({.grid = block.grid, .cell = block.cell});
      }
    });
    std::ranges::sort(list, z13::utility::DimmedLampBefore);
  }
  if (dimmed.list != list) {
    dimmed.list = std::move(list);
  }
}

void RegisterComponents(flecs::world world) {
  z13::flecs_tools::RegisterComponents<LampsFollowPower, DimmedLamps>(world);
}

void RegisterSystems(flecs::world world) {
  world.set(DimmedLamps {});
  world.set(LampsFollowPower {});
  const LampQuery lamps = world.query<const Block, const UtilitySupply>();
  world.system<DimmedLamps, LampsFollowPower, const PowerStatus, const BlockPalette>("LampSystem::Update")
      .kind<z13::utility::UtilityConsumersPhase>()
      .with<z13::station::StationMode>()
      .read<Block>()
      .read<UtilitySupply>()
      .each([lamps](DimmedLamps& dimmed, LampsFollowPower& follow, const PowerStatus& status,
                    const BlockPalette& palette) { Update(dimmed, follow, status, palette, lamps); });
}

}  // namespace

void LampSystem::Register(flecs::world& world) {
  z13::OnRegisterComponents(world, RegisterComponents);
  z13::OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::power_module
