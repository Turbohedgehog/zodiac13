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

#include <array>
#include <optional>

#include <lib_core/world/core_types.h>

#include <z13/components/action_id.h>
#include <z13/components/inventory.h>

namespace z13::inventory {

struct InventoryActionIds {
  using Singleton = void;
  std::array<std::optional<z13::input::ActionId>, kHotbarCells> select_cell;
  std::optional<z13::input::ActionId> next_cell;
  std::optional<z13::input::ActionId> previous_cell;
  std::optional<z13::input::ActionId> toggle_build_tool;
};

// The hotbar: hands out the starter kit, applies each player's cell picks from that player's
// own actions (so every participant and a replay pick alike) and derives ActiveItem.
class InventorySystem {
 public:
  static void Register(flecs::world& world);
};

}  // namespace z13::inventory
