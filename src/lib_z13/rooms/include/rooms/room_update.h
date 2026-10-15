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

#include <rooms/parallel_for.h>
#include <rooms/room_graph.h>
#include <rooms/room_sources.h>

namespace z13::station::rooms {

// `previous` rebuilt for `sources`: only the chunks the changed sealed boxes touch, the
// rooms around them and their portals; the rest is shared with `previous`. A room keeps its
// id while it lasts: split, its id stays with the part holding its old anchor; merged, the
// one with the lowest anchor keeps its id. New rooms get ids from `first_id` on, by anchor.
// Built whole without a previous graph or with other settings.
std::expected<RoomGraph, std::string> UpdateRooms(const RoomGraph& previous, const RoomSources& sources,
                                                  RoomId first_id, const RoomSettings& settings,
                                                  const ParallelFor& parallel = Serial);

// The same for the sources `edits` makes of `previous.sources`, without comparing them all;
// fails when it removes a source `previous` lacks.
std::expected<RoomGraph, std::string> UpdateRooms(const RoomGraph& previous, const RoomSourceEdits& edits,
                                                  RoomId first_id, const RoomSettings& settings,
                                                  const ParallelFor& parallel = Serial);

}  // namespace z13::station::rooms
