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

#include <z13/components/station.h>
#include <z13_primitives/palette.h>
#include <z13_primitives/placement.h>

namespace z13::station::rooms {

enum class Axis : uint8_t { kX, kY, kZ };

// Where a door or window block lets rooms see (or pass) into each other. The block itself
// is found by the opening's cell (BlockIndex): entity ids differ between peers and rebuilds.
struct PortalSource {
  // The cells the opening spans, through the block's thickness.
  z13::building::primitives::CellBox opening;
  // The world axis the opening is crossed along.
  Axis axis {};
  bool visible {};
  bool passable {};
};

// A block seals gas when its primitive says so; doors and windows seal until opened.
bool IsSealing(const z13::building::primitives::Primitive& primitive);

// The opening of a door frame (the gap between its posts) or of a transparent block (all
// of it); nothing for other blocks.
std::optional<PortalSource> PortalOf(
    const z13::station::Block& block, const z13::building::primitives::Primitive& primitive);

}  // namespace z13::station::rooms
