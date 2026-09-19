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

#include <expected>
#include <string>
#include <string_view>

#include <flecs.h>

#include <lib_core/world_serializer.h>

namespace z13::flecs_tools {

// World state as JSON: {"version": 1, "entities": [...]} with embedded component
// values. What is captured follows the state markers; transport is up to the caller.
class WorldJsonStore {
 public:
  static constexpr int kVersion = 1;

  // Fails if a component value cannot be written as JSON (e.g. NaN or infinity), so
  // a save that could not be loaded back is never produced.
  static std::expected<std::string, std::string> ToJson(const WorldSnapshot& snapshot);
  static std::expected<WorldSnapshot, std::string> FromJson(std::string_view json);

  static std::expected<std::string, std::string> Save(const flecs::world& world);

  // Parses and validates `json` before touching the world (see RestoreWorld). Must be
  // called between frames; from inside a frame use RequestLoadWorldState.
  static std::expected<void, std::string> Load(flecs::world& world, std::string_view json);
};

}  // namespace z13::flecs_tools
