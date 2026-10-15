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

#include "held_tool.h"

#include <z13/components/building.h>
#include <z13/components/inventory.h>
#include <z13/components/player.h>

namespace z13::building {

std::optional<flecs::entity> HeldBuildTool(flecs::entity player) {
  const auto* active = player.try_get<z13::inventory::ActiveItem>();
  if (active == nullptr) {
    return std::nullopt;
  }
  const flecs::entity item = player.world().entity(active->item);
  if (!item.is_alive() || !item.has<BuildTool>()) {
    return std::nullopt;
  }
  return item;
}

std::optional<flecs::entity> CarriedBuildTool(flecs::entity player) {
  const uint32_t player_id = player.get<z13::gameplay::Player>().id;
  std::optional<flecs::entity> tool;
  player.world().each([&tool, player_id](flecs::entity item, const z13::inventory::CarriedBy& carried) {
    if (carried.player_id == player_id && item.has<BuildTool>() &&
        (!tool || carried.cell < tool->get<z13::inventory::CarriedBy>().cell)) {
      tool = item;
    }
  });
  return tool;
}

}  // namespace z13::building
