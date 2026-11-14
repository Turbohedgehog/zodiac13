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
#include <span>

#include <flecs.h>

#include <z13/components/gameplay/access.h>
#include <z13/components/gameplay/scene_mesh.h>
#include <z13/components/station/block.h>
#include <primitives/geometry.h>

namespace z13::door_render {

// What the lamp over a door says to the local player.
enum class DoorSignal : uint8_t {
  kOpens,
  kNoAccess,
  kLocked,
};

// Locked by an interlock beats no access; a door without a requirement opens.
DoorSignal SignalOf(bool interlocked, bool may_use);

// The lamp's colour for each signal: green, yellow, red.
z13::Rgba ColorOf(DoorSignal signal);

// Two small emissive boxes, one on each face of the closed door near its top, in the grid's meters; nothing for a
// block without a leaf.
std::optional<z13::render::SceneMesh> BuildLampMesh(const z13::station::Block& block, DoorSignal signal,
                                                    z13::building::primitives::OptionalPalette palette);

}  // namespace z13::door_render
