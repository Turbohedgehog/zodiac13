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

#include "block_entities.h"

#include <Eigen/Dense>

#include <lib_core/state/world_state.h>

namespace z13::building {

namespace {

using z13::station::Block;
using z13::station::kCellSize;
using z13::station::SpawnPoint;
using z13::building::primitives::CellBox;
using z13::building::primitives::OccupiedCells;
using z13::building::primitives::OrientationMatrix;

// Upright whatever the marker's turn: only its +X, flattened, sets the facing. Built from
// integers, not trig, so every platform gets the same bits.
Eigen::Matrix4f SpawnTransform(const Block& block, float height_above_marker) {
  const CellBox cells = OccupiedCells(block);
  const Eigen::Vector3f center = (cells.min + cells.End()).cast<float>() / 2.f * kCellSize;
  const float top = static_cast<float>(cells.End().z()) * kCellSize;

  Eigen::Vector3i forward = OrientationMatrix(block.spec.orientation).col(0);
  forward.z() = 0;
  if (forward.isZero()) {
    forward = Eigen::Vector3i::UnitX();
  }
  Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
  transform.block<3, 1>(0, 0) = forward.cast<float>();
  transform.block<3, 1>(0, 1) = Eigen::Vector3f(static_cast<float>(-forward.y()), static_cast<float>(forward.x()), 0.f);
  transform.block<3, 1>(0, 3) = Eigen::Vector3f(center.x(), center.y(), top + height_above_marker);
  return transform;
}

}  // namespace

CellBox SpawnClearance(const Block& marker, const BuildingTuning& tuning) {
  const CellBox cells = OccupiedCells(marker);
  return {
      .min = {cells.min.x(), cells.min.y(), cells.End().z()},
      .extent = {cells.extent.x(), cells.extent.y(), tuning.spawn_clearance_cells},
  };
}

flecs::entity CreateBlock(
    flecs::world world, const std::string& name, const Block& block, const z13::building::primitives::Palette& palette,
    const BuildingTuning& tuning) {
  flecs::entity entity = world.entity(name.c_str()).add<z13::flecs_tools::StateEntity>().set(block);
  const auto primitive = palette.Find(block.spec.type_id);
  if (primitive && primitive->get().Has(z13::building::primitives::PrimitiveFlags::Spawn)) {
    entity.set(SpawnPoint {.transform = SpawnTransform(block, tuning.spawn_height_above_marker)});
  }
  return entity;
}

}  // namespace z13::building
