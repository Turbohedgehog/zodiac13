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

#include <atmos/air.h>
#include <rooms/room_graph.h>
#include <z13/components/utility/atmosphere.h>

namespace z13::atmos_module {

z13::utility::atmos::Air AirOf(const z13::utility::RoomAirEntry& entry);
z13::utility::RoomAirEntry EntryOf(const Eigen::Vector3i& anchor, const z13::utility::atmos::Air& air);

std::vector<z13::utility::RoomAirEntry> SeedRooms(const z13::station::rooms::RoomGraph& graph);

// Each room's air is split by the cells it shares with the rooms of `after`: what merges mixes, what splits shares out
// by volume, what lands in the vacuum is lost, and a new room nothing flowed into is empty.
std::vector<z13::utility::RoomAirEntry> CarryAir(const std::vector<z13::utility::RoomAirEntry>& air,
                                                 const z13::station::rooms::RoomGraph& before,
                                                 const z13::station::rooms::RoomGraph& after);

// The same without the old graph: a room whose anchor stays keeps its air, the others start empty.
std::vector<z13::utility::RoomAirEntry> KeepAirByAnchor(const std::vector<z13::utility::RoomAirEntry>& air,
                                                        const z13::station::rooms::RoomGraph& after);

}  // namespace z13::atmos_module
