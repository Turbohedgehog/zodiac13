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

#include <lib_core/state/snapshot_hash.h>

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <lib_core/state/snapshot_grouping.h>
#include <lib_core/utils/fnv_hash.h>

namespace z13::flecs_tools {

namespace {

uint64_t Mix(uint64_t hash, uint64_t value) {
  return MixHash(hash ^ value);
}

// Length first, so neighboring fields can't trade bytes.
uint64_t Mix(uint64_t hash, std::span<const std::byte> bytes) {
  return FnvHash(bytes, Mix(hash, bytes.size()));
}

uint64_t Mix(uint64_t hash, const std::string& text) {
  return Mix(hash, std::as_bytes(std::span(text)));
}

uint64_t Mix(uint64_t hash, const EntitySnapshot& entity) {
  hash = Mix(hash, entity.name);
  hash = Mix(hash, entity.tags.size());
  for (const std::string& tag : entity.tags) {
    hash = Mix(hash, tag);
  }
  hash = Mix(hash, entity.components.size());
  for (const ComponentValue& component : entity.components) {
    hash = Mix(Mix(hash, component.type), std::as_bytes(std::span(component.value)));
  }
  hash = Mix(hash, entity.relationships.size());
  for (const Relationship& relationship : entity.relationships) {
    hash = Mix(Mix(hash, relationship.relation), relationship.target);
  }
  return hash;
}

}  // namespace

uint64_t HashSnapshot(const WorldSnapshot& snapshot) {
  uint64_t hash = Mix(kFnvOffsetBasis, snapshot.entities.size());
  for (const EntitySnapshot& entity : snapshot.entities) {
    hash = Mix(hash, entity);
  }
  hash = Mix(hash, snapshot.groups.size());
  for (const std::shared_ptr<const GroupSnapshot>& group : snapshot.groups) {
    hash = Mix(Mix(hash, group->id), group->fingerprint);
  }
  return hash;
}

}  // namespace z13::flecs_tools
