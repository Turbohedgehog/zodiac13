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

#include <lib_core/state/world_json_store.h>

#include <algorithm>
#include <format>
#include <iterator>
#include <string_view>
#include <optional>
#include <utility>
#include <vector>

#include <lib_core/state/component_codec.h>
#include <lib_core/version.h>
#include <lib_core/utils/status.h>

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

using rfl::Generic;

// The document is built and read as Generic rather than a reflected struct:
// MSVC 14.52 rejects rfl::view_t for the struct (C3539).
constexpr std::string_view kVersionKey = "version";
constexpr std::string_view kEngineVersionKey = "engine_version";
constexpr std::string_view kEntitiesKey = "entities";
constexpr std::string_view kNameKey = "name";
constexpr std::string_view kTagsKey = "tags";
constexpr std::string_view kComponentsKey = "components";
constexpr std::string_view kRelationshipsKey = "relationships";
constexpr std::string_view kTypeKey = "type";
constexpr std::string_view kValueKey = "value";
constexpr std::string_view kRelationKey = "relation";
constexpr std::string_view kTargetKey = "target";

std::expected<Generic, std::string> ParseValue(const std::string& text) {
  auto value = rfl::json::read<Generic>(text);
  if (!value) {
    return std::unexpected(std::string(value.error().what()));
  }
  return *value;
}

template <class T>
std::expected<T, std::string> Unwrap(rfl::Result<T> result, std::string_view what) {
  if (!result) {
    return std::unexpected(std::format("{}: {}", what, result.error().what()));
  }
  return std::move(*result);
}

std::expected<Generic, std::string> Field(const Generic::Object& object, std::string_view key) {
  return Unwrap(object.get(std::string(key)), key);
}

std::expected<Generic::Object, std::string> ObjectField(const Generic::Object& object, std::string_view key) {
  return Field(object, key).and_then([key](const Generic& g) { return Unwrap(g.to_object(), key); });
}

std::expected<Generic::Array, std::string> ArrayField(const Generic::Object& object, std::string_view key) {
  return Field(object, key).and_then([key](const Generic& g) { return Unwrap(g.to_array(), key); });
}

std::expected<std::string, std::string> StringField(const Generic::Object& object, std::string_view key) {
  return Field(object, key).and_then([key](const Generic& g) { return Unwrap(g.to_string(), key); });
}

void Set(Generic::Object& object, std::string_view key, Generic value) {
  object.insert(std::string(key), std::move(value));
}

Generic::Array StringsJson(const std::vector<std::string>& strings) {
  Generic::Array array;
  std::ranges::transform(strings, std::back_inserter(array), [](const std::string& s) { return Generic(s); });
  return array;
}

Generic RelationshipJson(const Relationship& relationship) {
  Generic::Object object;
  Set(object, kRelationKey, Generic(relationship.relation));
  Set(object, kTargetKey, Generic(relationship.target));
  return object;
}

std::expected<std::vector<std::string>, std::string> StringsFrom(const Generic::Object& object, std::string_view key) {
  auto array = ArrayField(object, key);
  if (!array) {
    return std::unexpected(std::move(array.error()));
  }
  std::vector<std::string> strings;
  for (const Generic& item : *array) {
    auto text = Unwrap(item.to_string(), key);
    if (!text) {
      return std::unexpected(std::move(text.error()));
    }
    strings.push_back(std::move(*text));
  }
  return strings;
}

std::expected<std::vector<Relationship>, std::string> RelationshipsFrom(const Generic::Object& object) {
  auto array = ArrayField(object, kRelationshipsKey);
  if (!array) {
    return std::unexpected(std::move(array.error()));
  }
  std::vector<Relationship> relationships;
  for (const Generic& item : *array) {
    auto fields = Unwrap(item.to_object(), kRelationshipsKey);
    if (!fields) {
      return std::unexpected(std::move(fields.error()));
    }
    auto relation = StringField(*fields, kRelationKey);
    auto target = StringField(*fields, kTargetKey);
    if (!relation || !target) {
      return std::unexpected(relation ? target.error() : relation.error());
    }
    relationships.push_back({std::move(*relation), std::move(*target)});
  }
  return relationships;
}

}  // namespace

std::expected<std::string, std::string> WorldJsonStore::ToJson(
    const flecs::world& world, const WorldSnapshot& snapshot) {
  Generic::Array entities;
  for (const EntitySnapshot& s : AllEntities(snapshot)) {
    Generic::Array components;
    for (const auto& c : s.components) {
      const flecs::entity component = world.lookup(c.type.c_str());
      if (!component) {
        return std::unexpected(std::format("unknown component '{}' on '{}'", c.type, s.name));
      }
      auto value = ValueToJson(world, component, c.value).and_then(ParseValue);
      if (!value) {
        return std::unexpected(std::format("value of '{}' on '{}': {}", c.type, s.name, value.error()));
      }
      Generic::Object entry;
      Set(entry, kTypeKey, Generic(c.type));
      Set(entry, kValueKey, std::move(*value));
      components.push_back(std::move(entry));
    }
    Generic::Array relationships;
    std::ranges::transform(s.relationships, std::back_inserter(relationships), RelationshipJson);
    Generic::Object entity;
    Set(entity, kNameKey, Generic(s.name));
    Set(entity, kTagsKey, StringsJson(s.tags));
    Set(entity, kComponentsKey, std::move(components));
    Set(entity, kRelationshipsKey, std::move(relationships));
    entities.push_back(std::move(entity));
  }
  Generic::Object document;
  Set(document, kVersionKey, Generic(int64_t {kVersion}));
  Set(document, kEngineVersionKey, Generic(std::string(kEngineVersion)));
  Set(document, kEntitiesKey, std::move(entities));
  return rfl::json::write(Generic(std::move(document)), rfl::json::pretty);
}

std::expected<WorldSnapshot, std::string> WorldJsonStore::FromJson(
    const flecs::world& world, std::string_view json) {
  const auto root = ParseValue(std::string(json)).and_then([](const Generic& g) { return Unwrap(g.to_object(), "document"); });
  if (!root) {
    return std::unexpected(std::format("invalid world state JSON: {}", root.error()));
  }
  const auto version = Field(*root, kVersionKey).and_then([](const Generic& g) { return Unwrap(g.to_int(), kVersionKey); });
  if (!version) {
    return std::unexpected(std::format("invalid world state JSON: {}", version.error()));
  }
  if (*version != kVersion) {
    return std::unexpected(std::format("unsupported world state version {}", *version));
  }
  const auto entities = ArrayField(*root, kEntitiesKey);
  if (!entities) {
    return std::unexpected(std::format("invalid world state JSON: {}", entities.error()));
  }

  WorldSnapshot snapshot;
  for (const Generic& item : *entities) {
    const auto e = Unwrap(item.to_object(), kEntitiesKey);
    const auto name = e ? StringField(*e, kNameKey) : std::unexpected(e.error());
    const auto tags = e ? StringsFrom(*e, kTagsKey) : std::unexpected(e.error());
    const auto relationships = e ? RelationshipsFrom(*e) : std::unexpected(e.error());
    const auto components = e ? ArrayField(*e, kComponentsKey) : std::unexpected(e.error());
    if (!name || !tags || !relationships || !components) {
      const std::string& error = !name ? name.error() : !tags ? tags.error() : !relationships ? relationships.error() : components.error();
      return std::unexpected(std::format("invalid world state JSON: {}", error));
    }
    EntitySnapshot s {.name = *name, .tags = *tags, .relationships = *relationships};
    for (const Generic& entry : *components) {
      const auto fields = Unwrap(entry.to_object(), kComponentsKey);
      const auto type = fields ? StringField(*fields, kTypeKey) : std::unexpected(fields.error());
      const auto value = fields ? Field(*fields, kValueKey) : std::unexpected(fields.error());
      if (!type || !value) {
        return std::unexpected(std::format("invalid world state JSON: {}", type ? value.error() : type.error()));
      }
      const flecs::entity component = world.lookup(type->c_str());
      if (!component) {
        return std::unexpected(std::format("unknown component '{}' on '{}'", *type, *name));
      }
      auto bytes = ValueFromJson(world, component, rfl::json::write(*value));
      if (!bytes) {
        return std::unexpected(std::format("value of '{}' on '{}': {}", *type, *name, bytes.error()));
      }
      s.components.push_back({*type, std::move(*bytes)});
    }
    snapshot.entities.push_back(ShareEntity(std::move(s)));
  }
  return snapshot;
}

std::expected<std::string, std::string> WorldJsonStore::Save(const flecs::world& world) {
  return CaptureState(world).and_then([&world](const WorldSnapshot& snapshot) { return ToJson(world, snapshot); });
}

Status WorldJsonStore::Load(flecs::world& world, std::string_view json) {
  auto snapshot = FromJson(world, json);
  if (!snapshot) {
    return std::unexpected(std::move(snapshot.error()));
  }
  return RestoreWorld(world, *snapshot);
}

}  // namespace z13::flecs_tools
