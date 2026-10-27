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

#include "inventory_system.h"

#include <algorithm>
#include <format>
#include <string>
#include <vector>

#include <flecs.h>

#include <actions_generated.h>

#include <lib_core/state/state_markers.h>
#include <lib_core/state/world_state.h>
#include <lib_core/time/simulation_clock.h>
#include <lib_core/utils/log.h>
#include <lib_core/world/lifecycle.h>
#include <z13/components/action_listener.h>
#include <z13/components/action_map.h>
#include <z13/components/building.h>
#include <z13/components/gameplay.h>
#include <z13/components/input_events.h>
#include <z13/components/input_phases.h>
#include <z13/components/player.h>
#include <z13/components/spawn_stamp.h>
#include <z13_module/input/input_config_loader.h>

namespace z13::inventory {

namespace {

using z13::fbs::actions::Action;
using z13::gameplay::Player;
using ItemQuery = flecs::query<const Item, const CarriedBy>;

void OnConfigUpdated(
    InventoryActionIds& ids, z13::input::OnConfigUpdatedEvent, const z13::input::ActionMap& action_map) {
  const auto find = [&action_map](Action action) {
    const auto id = z13::gameplay::input::InputConfigLoader::FindActionId(
        action_map.action_map, z13::gameplay::input::kActionsEnumName, action);
    if (!id) {
      log_error("Inventory: cannot find action {} in '{}'", static_cast<int>(action),
                z13::gameplay::input::kActionsEnumName);
    }
    return id;
  };
  constexpr std::array kCellActions {
      Action::SELECT_CELL_1, Action::SELECT_CELL_2, Action::SELECT_CELL_3, Action::SELECT_CELL_4, Action::SELECT_CELL_5,
      Action::SELECT_CELL_6, Action::SELECT_CELL_7, Action::SELECT_CELL_8, Action::SELECT_CELL_9,
  };
  ids = {};
  std::ranges::transform(kCellActions, ids.select_cell.begin(), find);
  ids.next_cell = find(Action::NEXT_CELL);
  ids.previous_cell = find(Action::PREVIOUS_CELL);
  ids.toggle_build_tool = find(Action::TOGGLE_BUILD_TOOL);
}

bool IsSwitchedOn(const z13::input::ActionListener& listener, const std::optional<z13::input::ActionId>& id) {
  const auto value = id ? listener.Value(*id) : std::nullopt;
  return value && value->IsSwitchedOn();
}

// The one item a player is given in the tick it joins.
constexpr uint32_t kStarterToolIndex = 0;

void GiveStarterKit(flecs::entity player, const Player& owner) {
  flecs::world world = player.world();
  const z13::gameplay::SpawnStamp stamp {
      .tick = world.get<z13::flecs_tools::SimulationClock>().tick, .player_id = owner.id};
  world.entity(z13::gameplay::SpawnName("Item", stamp, kStarterToolIndex).c_str())
      .add<z13::flecs_tools::StateEntity>()
      .set(Item {.kind = ItemKind::kBuildTool})
      .set(CarriedBy {.player_id = owner.id, .cell = kStarterToolCell})
      .set(z13::building::BuildTool {});
  player.add<StarterKitGiven>();
  // SpawnPlayer gives every player one; a player made otherwise (an older save) gets it here.
  if (!player.has<Hotbar>()) {
    player.set(Hotbar {});
  }
}

std::optional<uint8_t> BuildToolCell(const ItemQuery& items, uint32_t player_id) {
  std::optional<uint8_t> cell;
  items.each([&cell, player_id](const Item& item, const CarriedBy& carried) {
    if (carried.player_id == player_id && item.kind == ItemKind::kBuildTool && (!cell || carried.cell < *cell)) {
      cell = carried.cell;
    }
  });
  return cell;
}

Hotbar Selected(const Hotbar& hotbar, uint8_t cell) {
  return cell == hotbar.active_cell ? hotbar : Hotbar {.active_cell = cell, .previous_cell = hotbar.active_cell};
}

Hotbar Stepped(const Hotbar& hotbar, int step) {
  const int cell = (hotbar.active_cell + step + kHotbarCells) % kHotbarCells;
  return Selected(hotbar, static_cast<uint8_t>(cell));
}

Hotbar ToggledBuildTool(const Hotbar& hotbar, std::optional<uint8_t> tool_cell) {
  if (!tool_cell) {
    return hotbar;
  }
  return Selected(hotbar, hotbar.active_cell == *tool_cell ? hotbar.previous_cell : *tool_cell);
}

void SelectCell(
    const ItemQuery& items, const Player& player, Hotbar& hotbar, const z13::input::ActionListener& listener,
    const InventoryActionIds& ids) {
  Hotbar picked = hotbar;
  for (uint8_t cell = 0; cell < kHotbarCells; ++cell) {
    if (IsSwitchedOn(listener, ids.select_cell[cell])) {
      picked = Selected(picked, cell);
    }
  }
  const int step = (IsSwitchedOn(listener, ids.next_cell) ? 1 : 0) - (IsSwitchedOn(listener, ids.previous_cell) ? 1 : 0);
  if (step != 0) {
    picked = Stepped(picked, step);
  }
  if (IsSwitchedOn(listener, ids.toggle_build_tool)) {
    picked = ToggledBuildTool(picked, BuildToolCell(items, player.id));
  }
  hotbar = picked;
}

void DeriveActiveItem(const ItemQuery& items, flecs::entity player, const Player& owner, const Hotbar& hotbar) {
  std::optional<flecs::entity_t> active;
  items.each([&active, &owner, &hotbar](flecs::entity item, const Item&, const CarriedBy& carried) {
    if (carried.player_id == owner.id && carried.cell == hotbar.active_cell) {
      active = item.id();
    }
  });
  if (!active) {
    player.remove<ActiveItem>();
  } else if (!player.has<ActiveItem>() || player.get<ActiveItem>().item != *active) {
    player.set(ActiveItem {.item = *active});
  }
}

void DropItemsOfLeftPlayers(flecs::iter& it) {
  std::vector<uint32_t> present;
  it.world().each([&present](const Player& player) { present.push_back(player.id); });
  while (it.next()) {
    const auto carried = it.field<const CarriedBy>(0);
    for (const size_t i : it) {
      if (!std::ranges::contains(present, carried[i].player_id)) {
        it.entity(i).destruct();
      }
    }
  }
}

void RegisterComponents(flecs::world world) {
  z13::flecs_tools::RegisterComponents<Item, CarriedBy, Hotbar, StarterKitGiven, ActiveItem, InventoryActionIds>(world);
}

void RegisterPhases(flecs::world world) {
  world.component<InventoryPhase>().add(flecs::Phase).depends_on<z13::input::RemoteActionFramePhase>();
  world.component<z13::input::ApplyActionFramePhase>().depends_on<InventoryPhase>();
}

// One phase, in this order: the kit, then the picks (the tool key needs the kit's tool), then
// ActiveItem; each in player id order.
void RegisterSystems(flecs::world world) {
  world.set<InventoryActionIds>({});

  world.observer<InventoryActionIds, z13::input::OnConfigUpdatedEvent, z13::input::ActionMap>(
           "InventorySystem::OnConfigUpdated")
      .event<z13::input::SystemInputEventType>()
      .each(OnConfigUpdated);

  world.system<const Player>("InventorySystem::GiveStarterKit")
      .kind<InventoryPhase>()
      .without<StarterKitGiven>()
      .order_by<Player>(z13::gameplay::CompareByPlayerId)
      .write<Item>()
      .write<CarriedBy>()
      .write<z13::building::BuildTool>()
      .write<StarterKitGiven>()
      .write<Hotbar>()
      .each(GiveStarterKit);

  const ItemQuery items = world.query_builder<const Item, const CarriedBy>("InventorySystem::Items").build();

  world.system<const Player, Hotbar, const z13::input::ActionListener, const InventoryActionIds>(
           "InventorySystem::SelectCell")
      .kind<InventoryPhase>()
      .order_by<Player>(z13::gameplay::CompareByPlayerId)
      .read<Item>()
      .read<CarriedBy>()
      .each([items](const Player& player, Hotbar& hotbar, const z13::input::ActionListener& listener,
                    const InventoryActionIds& ids) { SelectCell(items, player, hotbar, listener, ids); });

  world.system<const Player, const Hotbar>("InventorySystem::DeriveActiveItem")
      .kind<InventoryPhase>()
      .read<Item>()
      .read<CarriedBy>()
      .write<ActiveItem>()
      .each([items](flecs::entity player, const Player& owner, const Hotbar& hotbar) {
        DeriveActiveItem(items, player, owner, hotbar);
      });

  world.system<const CarriedBy>("InventorySystem::DropItemsOfLeftPlayers")
      .kind<InventoryPhase>()
      .with<Item>()
      .write<Item>()
      .run(DropItemsOfLeftPlayers);
}

}  // namespace

void InventorySystem::Register(flecs::world& world) {
  OnRegisterComponents(world, RegisterComponents);
  OnInitPhases(world, RegisterPhases);
  OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::inventory
