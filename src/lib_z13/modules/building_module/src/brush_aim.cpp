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

#include "brush_aim.h"

#include <algorithm>
#include <cmath>
#include <optional>

#include <z13_primitives/placement.h>

namespace z13::building {

using z13::building::primitives::OccupiedCells;
using z13::station::Block;
using z13::station::BlockSpec;

Eigen::Vector3i Aim::Cell() const {
  return (point + normal.cast<float>() / 2.f).array().floor().cast<int>();
}

Aim AimAt(const grid::BlockIndex& index, const Eigen::Vector3f& eye, const Eigen::Vector3f& reach) {
  const auto hit = index.RaycastHit(eye, reach);
  return hit ? Aim {.eye = eye, .point = hit->point, .normal = hit->normal} : Aim {.eye = eye, .point = reach};
}

namespace {

// How far along the face a block may slide to clear what it would sink into.
constexpr int kMaxSlideCells = 8;

// `block` moved along its face (the axes `normal` is zero on) to the nearest place on free
// cells; unchanged when it is free already or nothing near is.
Block SlidAlongFace(const Block& block, const Eigen::Vector3i& normal, const grid::BlockIndex& index) {
  if (!index.Overlaps(OccupiedCells(block))) {
    return block;
  }
  const Eigen::Vector3i along = (normal.array() == 0).cast<int>();
  std::optional<Block> best;
  int best_distance = 0;
  for (int ring = 1; ring <= kMaxSlideCells && !best; ++ring) {
    for (int x = -ring; x <= ring; ++x) {
      for (int y = -ring; y <= ring; ++y) {
        for (int z = -ring; z <= ring; ++z) {
          const Eigen::Vector3i shift {x, y, z};
          if (shift.cwiseAbs().maxCoeff() != ring || (shift.array() * (1 - along.array())).any()) {
            continue;
          }
          Block moved = block;
          moved.cell += shift;
          const int distance = static_cast<int>(shift.squaredNorm());
          if ((!best || distance < best_distance) && !index.Overlaps(OccupiedCells(moved))) {
            best = moved;
            best_distance = distance;
          }
        }
      }
    }
  }
  return best.value_or(block);
}

}  // namespace

Block BlockAt(const Aim& aim, const BlockSpec& spec, const grid::BlockIndex& index) {
  if (!aim.normal.isZero()) {
    return SlidAlongFace(z13::building::primitives::PlaceOnFace(aim.point, aim.normal, spec), aim.normal, index);
  }
  // One cell per step: a block never jumps over the blocks it would sink into.
  const Eigen::Vector3f back = aim.eye - aim.point;
  const int steps = std::max(1, static_cast<int>(std::ceil(back.norm())));
  for (int step = 0; step <= steps; ++step) {
    const Eigen::Vector3f point = aim.point + back * (static_cast<float>(step) / static_cast<float>(steps));
    const Block block = z13::building::primitives::PlaceCentredOn(point, spec);
    if (!index.Overlaps(OccupiedCells(block))) {
      return block;
    }
  }
  return z13::building::primitives::PlaceCentredOn(aim.point, spec);
}

}  // namespace z13::building
