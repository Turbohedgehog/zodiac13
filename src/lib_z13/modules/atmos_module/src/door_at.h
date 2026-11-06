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

#include <functional>
#include <optional>
#include <span>
#include <vector>

#include <flecs.h>

#include <primitives/palette.h>
#include <primitives/cell_box.h>
#include <z13/components/station/block.h>
#include <z13/components/station/door.h>

namespace z13::atmos_module {

using DoorQuery = flecs::query<const z13::station::DoorState, const z13::station::Block>;

// A door of the main grid with the openings its block closes.
struct DoorAt {
  flecs::entity entity;
  std::vector<z13::building::primitives::CellBox> openings;
  z13::station::DoorPosition position {};
};

std::vector<DoorAt> DoorsOf(const DoorQuery& doors, const z13::building::primitives::BlockPalette& palette);

bool Overlaps(const DoorAt& door, const z13::building::primitives::CellBox& opening);

std::optional<std::reference_wrapper<const DoorAt>> DoorAtOpening(std::span<const DoorAt> doors,
                                                                 const z13::building::primitives::CellBox& opening);

}  // namespace z13::atmos_module
