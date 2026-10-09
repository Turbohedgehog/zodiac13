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

#include <flecs.h>

#include <z13/components/station.h>
#include <primitives/palette.h>
#include <rooms/room_builder.h>

namespace z13::station {

struct TopologySources {
  z13::station::rooms::RoomSources sources;
  uint64_t fingerprint {};
};

TopologySources GatherSources(
    const flecs::query<const Block>& blocks, const z13::building::primitives::BlockPalette& palette);

}  // namespace z13::station
