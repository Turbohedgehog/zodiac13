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
#include <string>
#include <vector>

#include <lib_core/state/world_serializer.h>

// reflect-cpp's headers trigger warnings under /W4 that this project treats as
// errors; suppress them for code this project doesn't own.
#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include <rfl/Generic.hpp>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace z13::flecs_tools {

// Wire twins of the snapshot types: component values are embedded JSON, not strings.
struct ComponentJson {
  std::string type;
  rfl::Generic value;
};

struct EntityJson {
  std::string name;
  std::vector<std::string> tags;
  std::vector<ComponentJson> components;
  std::vector<Relationship> relationships;
};

struct DocumentJson {
  int version {};
  // Which build made the save; informational only, not checked on load (see
  // WorldJsonStore's class comment). Optional so older/hand-written JSON without
  // this field still parses.
  std::optional<std::string> engine_version;
  std::vector<EntityJson> entities;
};

}  // namespace z13::flecs_tools
