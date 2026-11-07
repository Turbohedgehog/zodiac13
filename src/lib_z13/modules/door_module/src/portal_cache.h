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

#include <unordered_map>
#include <vector>

#include <flecs.h>

#include <primitives/palette.h>
#include <z13/components/station/block.h>
#include <z13/components/station/door.h>

namespace z13::door_module {

struct PortalEntry {
  z13::station::Block block;
  std::vector<z13::station::DoorOpening> openings;
};

// The portal openings of each door by its block, so a door that has not moved is not asked for them again.
class PortalCache {
 public:
  // The openings of the main grid's doors that are not shut, sorted.
  std::vector<z13::station::DoorOpening> OpenOf(
      const flecs::query<const z13::station::DoorState, const z13::station::Block>& doors,
      const z13::building::primitives::BlockPalette& palette);

 private:
  std::unordered_map<flecs::entity_t, PortalEntry> entries_;
};

}  // namespace z13::door_module
