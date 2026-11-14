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

#include <optional>
#include <span>
#include <string>

#include <flecs.h>

#include <lib_core/commands/command_registry.h>

namespace z13::console_input {

// A line whose first word is a setting's path, as in Quake: `render.shadow_lights` replies with its value,
// `render.shadow_lights 4` changes it for this run and replies with the value it has now. A table's name
// followed by a dot (`render.`) lists its settings. Empty when the first word names no setting.
std::optional<CommandResult> RunVariable(flecs::world world, std::span<const std::string> words);

}  // namespace z13::console_input
