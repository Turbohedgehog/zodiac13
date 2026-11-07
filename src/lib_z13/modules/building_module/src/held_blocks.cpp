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

#include "held_blocks.h"

#include <algorithm>
#include <array>

#include <primitives/placement.h>
#include <sectors/sector.h>

namespace z13::building {

namespace {

bool SectorBefore(const Eigen::Vector3i& a, const Eigen::Vector3i& b) {
  return std::ranges::lexicographical_compare(std::array {a.x(), a.y(), a.z()}, std::array {b.x(), b.y(), b.z()});
}

}  // namespace

bool TouchesHeldSectors(const z13::station::Block& block, const z13::station::HeldSectors& held,
                        const z13::station::GridPoses& poses) {
  if (!held.sectors || held.sector_cells <= 0) {
    return true;
  }
  const auto touched = z13::net::sectors::SectorsTouched(
      z13::building::primitives::OccupiedCells(block), z13::station::PoseOfGrid(poses, block.grid), held.sector_cells);
  return std::ranges::any_of(touched, [&held](const Eigen::Vector3i& sector) {
    return std::ranges::binary_search(*held.sectors, sector, SectorBefore);
  });
}

}  // namespace z13::building
