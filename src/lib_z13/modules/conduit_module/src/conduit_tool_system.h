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
#include <optional>

#include <flecs.h>

#include <z13/components/action_listener.h>
#include <z13/components/inventory.h>

namespace z13::conduit {

// The conduit tool goes into the cell after the one players spawn with active, next to the build tool.
inline constexpr uint8_t kConduitToolCell = z13::inventory::kSpawnActiveCell + 1;

// A player was given the conduit tool; one who lost it doesn't get it back.
struct ConduitToolGiven {
  using State = void;
};

// The tool's actions' ids in the loaded action map, refreshed when the input config changes.
struct ConduitActionIds {
  using Singleton = void;
  using IdType = z13::input::ActionId;
  std::optional<IdType> lay;
  std::optional<IdType> remove;
  std::optional<IdType> previous_kind;
  std::optional<IdType> next_kind;
  std::optional<IdType> toggle_depth;
};

// The conduit tool: handed out with the starter kit in station mode; in the hand, it lays runs of the chosen kind
// through walls and floors and removes them.
class ConduitToolSystem {
 public:
  static void Register(flecs::world& world);
};

}  // namespace z13::conduit
