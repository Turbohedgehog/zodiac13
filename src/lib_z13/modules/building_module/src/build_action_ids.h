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
#include <cstddef>
#include <optional>

#include <z13/components/input.h>

namespace z13::building {

inline constexpr size_t kPaletteSlots = 9;

// The building actions' ids in the loaded action map, refreshed when the input config changes.
struct BuildActionIds {
  using Singleton = void;
  using IdType = z13::input::ActionInfo::IdType;
  std::optional<IdType> toggle_building_mode;
  std::optional<IdType> build_block;
  std::optional<IdType> destroy_block;
  std::optional<IdType> previous_primitive;
  std::optional<IdType> next_primitive;
  std::array<std::optional<IdType>, kPaletteSlots> select_slot;
  std::optional<IdType> rotate_around_z;
  std::optional<IdType> rotate_around_y;
  std::optional<IdType> rotate_around_x;
  std::optional<IdType> select_primitive;
};

// Whether the action went down this frame; false for an action the config lacks.
bool IsSwitchedOn(const z13::input::ActionListener& listener, const std::optional<BuildActionIds::IdType>& action_id);
bool IsSwitchedOff(const z13::input::ActionListener& listener, const std::optional<BuildActionIds::IdType>& action_id);

}  // namespace z13::building
