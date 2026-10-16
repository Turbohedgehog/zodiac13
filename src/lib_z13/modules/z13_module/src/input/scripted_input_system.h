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

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <lib_core/world/core_types.h>

#include "input_script.h"

namespace z13::gameplay::input {

// The --input-script being played: client-only, never state.
struct ScriptedInput {
  using Singleton = void;
  std::vector<ScriptStep> steps;
  // The tick the local player was first seen in the game; the steps' ticks count from it.
  std::optional<uint64_t> start_tick;
  size_t next {};
};

// Feeds the script's keys and turns into InputState, as the keyboard and mouse would, so they
// reach the simulation and the server like a player's; `quit` shuts the process down.
class ScriptedInputSystem {
 public:
  static void Register(flecs::world& world);
};

}  // namespace z13::gameplay::input
