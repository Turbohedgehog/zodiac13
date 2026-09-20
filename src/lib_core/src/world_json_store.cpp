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

#include <lib_core/world_json_store.h>

#include <format>
#include <optional>
#include <utility>
#include <vector>

#include <lib_core/version.h>

// reflect-cpp's headers trigger warnings under /W4 that this project treats as
// errors; suppress them for code this project doesn't own.
#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include <rfl/Generic.hpp>
#include <rfl/json.hpp>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace z13::flecs_tools {

namespace {

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

std::expected<rfl::Generic, std::string> ParseValue(const std::string& text) {
  auto value = rfl::json::read<rfl::Generic>(text);
  if (!value) {
    return std::unexpected(std::string(value.error().what()));
  }
  return *value;
}

}  // namespace

std::expected<std::string, std::string> WorldJsonStore::ToJson(const WorldSnapshot& snapshot) {
  DocumentJson document {.version = kVersion, .engine_version = std::string(kEngineVersion)};
  for (const auto& s : snapshot.entities) {
    EntityJson entity {.name = s.name, .tags = s.tags, .relationships = s.relationships};
    for (const auto& c : s.components) {
      auto value = ParseValue(c.value);
      if (!value) {
        return std::unexpected(
            std::format("value of '{}' on '{}' is not valid JSON (NaN or infinity?)", c.type, s.name));
      }
      entity.components.push_back({c.type, std::move(*value)});
    }
    document.entities.push_back(std::move(entity));
  }
  return rfl::json::write(document, rfl::json::pretty);
}

std::expected<WorldSnapshot, std::string> WorldJsonStore::FromJson(std::string_view json) {
  const auto document = rfl::json::read<DocumentJson>(std::string(json));
  if (!document) {
    return std::unexpected(std::format("invalid world state JSON: {}", document.error().what()));
  }
  if (document->version != kVersion) {
    return std::unexpected(std::format("unsupported world state version {}", document->version));
  }

  WorldSnapshot snapshot;
  for (const auto& e : document->entities) {
    EntitySnapshot s {.name = e.name, .tags = e.tags, .relationships = e.relationships};
    for (const auto& c : e.components) {
      s.components.push_back({c.type, rfl::json::write(c.value)});
    }
    snapshot.entities.push_back(std::move(s));
  }
  return snapshot;
}

std::expected<std::string, std::string> WorldJsonStore::Save(const flecs::world& world) {
  return ToJson(CaptureState(world));
}

std::expected<void, std::string> WorldJsonStore::Load(flecs::world& world, std::string_view json) {
  auto snapshot = FromJson(json);
  if (!snapshot) {
    return std::unexpected(std::move(snapshot.error()));
  }
  return RestoreWorld(world, *snapshot);
}

}  // namespace z13::flecs_tools
