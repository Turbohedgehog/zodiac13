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

#include <vector>

#include <flecs.h>

#include <z13/components/utility/atmosphere.h>

namespace z13::atmos_module {

using RoomAirQuery = flecs::query<const z13::utility::RoomAirEntry>;

std::vector<z13::utility::RoomAirEntry> LoadRoomAir(const RoomAirQuery& query);

// Makes the rooms' state entities hold exactly `rooms` (sorted by anchor): sets the ones that differ, makes the missing
// ones and destroys the others. Each is named by its anchor, as a restore matches state entities by name.
void StoreRoomAir(flecs::world world, const RoomAirQuery& query, const std::vector<z13::utility::RoomAirEntry>& rooms);

}  // namespace z13::atmos_module
