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

#include <z13/components/station/block.h>
#include <z13/components/station/grid.h>
#include <z13/components/station/held_sectors.h>
#include <z13/components/station/wall.h>

namespace z13::building {

struct PanelEntity {
  flecs::entity_t entity {};
  z13::station::WallPanelOf of;
  z13::station::Block block;

  bool operator==(const PanelEntity&) const = default;
};

// Everything the panels derive from, and the panels there are: WallPanelSystem derives again only when it differs from
// the last tick's.
struct PanelInputs {
  std::vector<z13::station::WallNode> nodes;
  std::vector<z13::station::WallSegment> segments;
  std::vector<z13::station::WallOpening> openings;
  std::vector<PanelEntity> panels;
  std::vector<z13::station::Grid> grids;
  z13::station::HeldSectors held;
  int joint_reach {};

  bool operator==(const PanelInputs&) const = default;
};

}  // namespace z13::building
