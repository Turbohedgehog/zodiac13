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

#include <cstddef>
#include <span>
#include <vector>

#include <Eigen/Dense>

#include <z13/components/station.h>
#include <primitives/geometry.h>

namespace z13::building::primitives {

// Indices into the blocks drawn: the opaque ones first, in any order, then the transparent
// ones from the farthest to the nearest, so each blends over what lies behind it.
struct DrawOrder {
  std::vector<size_t> opaque;
  std::vector<size_t> transparent;
};

// A block whose type the palette lacks is opaque.
bool IsTransparent(const z13::station::Block& block, OptionalPalette palette);

// `eye` in cells.
DrawOrder SortForDrawing(
    std::span<const z13::station::Block> blocks, OptionalPalette palette, const Eigen::Vector3f& eye);

}  // namespace z13::building::primitives
