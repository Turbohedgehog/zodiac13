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
#include <string>

#include <flecs.h>

#include <z13/components/hotbar.h>

// A player's items: one hotbar of cells, the active cell is the hand (docs/android-plan.md,
// "Инвентарь на экране"; dynamic-objects-plan.md, stage 2).
namespace z13::inventory {

// The starter kit's build tool goes into the first cell, and players spawn with the next one
// active: empty-handed, as before the tool was an item.
inline constexpr uint8_t kStarterToolCell = 0;
inline constexpr uint8_t kSpawnActiveCell = 1;

// Where cell selection applies, for every player, before the frame's actions read the hand.
struct InventoryPhase {};

enum class ItemKind : uint8_t { kBuildTool, kConduitTool };

struct Item {
  using State = void;
  ItemKind kind {};
};

// What the hotbar shows on an item, set every frame by the module that owns the item; never state.
struct ItemCaption {
  std::string text;
};

// The carrier by player id, which every participant shares, unlike entity ids.
struct CarriedBy {
  using State = void;
  using PlayerBound = void;
  uint32_t player_id {};
  uint8_t cell {};
};

// The active cell, and the one the tool key returns to.
struct Hotbar {
  using State = void;
  uint8_t active_cell = kSpawnActiveCell;
  uint8_t previous_cell = kSpawnActiveCell;
};

// The starter kit was handed out; a player who lost an item doesn't get it back.
struct StarterKitGiven {
  using State = void;
};

// The item in the active cell, derived every frame from Hotbar and CarriedBy; absent while
// the cell is empty.
struct ActiveItem {
  flecs::entity_t item {};
};

}  // namespace z13::inventory
