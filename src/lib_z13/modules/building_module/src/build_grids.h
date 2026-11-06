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
#include <vector>

#include <Eigen/Geometry>
#include <flecs.h>

#include <grid/block_index.h>
#include <z13/components/station/grid_id.h>

namespace z13::building {

// One grid the build tool can aim at: its pose and the entity that holds its BlockIndex.
struct GridView {
  z13::station::GridId id = z13::station::kMainGrid;
  Eigen::Isometry3f to_world = Eigen::Isometry3f::Identity();
  flecs::entity entity;

  const grid::BlockIndex& Index() const { return entity.get<grid::BlockIndex>(); }
  // For a system that declares it writes BlockIndex.
  grid::BlockIndex& MutableIndex() const { return entity.get_mut<grid::BlockIndex>(); }
  // A point from the world's meters into the grid's cells, and back.
  Eigen::Vector3f ToCells(const Eigen::Vector3f& world_meters) const;
  Eigen::Vector3f ToWorldMeters(const Eigen::Vector3f& cells) const;
};

// The first block along a world ray, among all grids.
struct GridHit {
  std::reference_wrapper<const GridView> grid;
  grid::BlockIndex::RayHit hit;  // in the grid's cells
};

// Every grid with a BlockIndex, by id.
class BuildGrids {
 public:
  static BuildGrids Of(flecs::world world);

  bool Empty() const { return grids_.empty(); }
  std::optional<std::reference_wrapper<const GridView>> Find(z13::station::GridId id) const;
  // The main grid, else the first; not for an empty set.
  const GridView& Fallback() const;
  // The block nearest `from_meters` along the segment to `to_meters` (world meters); of two at the same distance the
  // lower grid id's.
  std::optional<GridHit> Raycast(const Eigen::Vector3f& from_meters, const Eigen::Vector3f& to_meters) const;

 private:
  std::vector<GridView> grids_;
};

}  // namespace z13::building
