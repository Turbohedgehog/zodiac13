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

#include <primitives/placement.h>
#include <rooms/portal_source.h>

namespace z13::station::rooms {

// What the rooms are built from: the cells of the sealing blocks and the openings of the
// doors and windows among them.
struct RoomSources {
  std::vector<z13::building::primitives::CellBox> sealed;
  std::vector<PortalSource> portals;
};

// Sources added and removed since a graph was built.
struct RoomSourceEdits {
  RoomSources added;
  RoomSources removed;
};

}  // namespace z13::station::rooms
