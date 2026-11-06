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

#include "build_grids.h"

#include <algorithm>

#include <z13/components/station/block_spec.h>
#include <z13/components/station/grid.h>
#include <z13/components/station/grid_pose.h>

namespace z13::building {

using z13::station::kCellSize;

Eigen::Vector3f GridView::ToCells(const Eigen::Vector3f& world_meters) const {
  return ToLocalMeters(world_meters) / kCellSize;
}

Eigen::Vector3f GridView::ToWorldMeters(const Eigen::Vector3f& cells) const {
  return to_world * (cells * kCellSize);
}

BuildGrids BuildGrids::Of(flecs::world world) {
  BuildGrids found;
  world.query<const z13::station::Grid, const grid::BlockIndex>().each(
      [&found](flecs::entity entity, const z13::station::Grid& grid, const grid::BlockIndex&) {
        found.grids_.push_back({.id = grid.id, .to_world = z13::station::GridToWorld(grid), .entity = entity});
      });
  std::ranges::sort(found.grids_, {}, &GridView::id);
  return found;
}

std::optional<std::reference_wrapper<const GridView>> BuildGrids::Find(z13::station::GridId id) const {
  const auto found = std::ranges::find(grids_, id, &GridView::id);
  return found != grids_.end() ? std::optional(std::cref(*found)) : std::nullopt;
}

const GridView& BuildGrids::Fallback() const {
  const auto main = Find(z13::station::kMainGrid);
  return main ? main->get() : grids_.front();
}

std::optional<GridHit> BuildGrids::Raycast(const Eigen::Vector3f& from_meters, const Eigen::Vector3f& to_meters) const {
  std::optional<GridHit> nearest;
  float nearest_distance {};
  for (const GridView& grid : grids_) {
    const auto hit = grid.Index().RaycastHit(grid.ToCells(from_meters), grid.ToCells(to_meters));
    if (!hit) {
      continue;
    }
    const float distance = (grid.ToWorldMeters(hit->point) - from_meters).squaredNorm();
    if (!nearest || distance < nearest_distance) {
      nearest = GridHit {.grid = grid, .hit = *hit};
      nearest_distance = distance;
    }
  }
  return nearest;
}

}  // namespace z13::building
