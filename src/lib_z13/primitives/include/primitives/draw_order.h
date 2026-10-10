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

#include <span>

#include <Eigen/Dense>

#include <z13/components/station.h>
#include <primitives/geometry.h>

namespace z13::building::primitives {

// A block whose type the palette lacks is opaque.
bool IsTransparent(const z13::station::Block& block, OptionalPalette palette);

struct ChunkBounds {
  Eigen::Vector3i key = Eigen::Vector3i::Zero();
  Eigen::AlignedBox3f bounds;
};

// Sorts `chunks` from the farthest centre to the nearest, so each chunk's glass blends over
// what lies behind it; equally far ones by key, whatever order they come in. `eye` in the
// units of the bounds.
void SortFarthestFirst(std::span<ChunkBounds> chunks, const Eigen::Vector3f& eye);

}  // namespace z13::building::primitives
