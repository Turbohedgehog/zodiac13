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
#include <expected>
#include <span>
#include <string>

#include <Eigen/Core>

#include <z13/components/block.h>
#include <primitives/palette.h>

namespace z13::building::primitives {

constexpr size_t kTriangleCorners = 3;
constexpr size_t kSectionCorners = 4;

// The world axes (0 to 2) that a Panel's own X, Y and Z run along.
struct PrismFrame {
  int x_axis {};
  int y_axis {};
  int z_axis {};
};

// One corner of a panel's section: where it stands across the Y axis (along x_axis, then
// z_axis) and the two world coordinates along y_axis that its edge runs between.
struct PrismCorner {
  Eigen::Vector2i plan = Eigen::Vector2i::Zero();
  int from {};
  int to {};
};

// The Panel block whose section has these three or four corners, in order round the section,
// each with its own edge along Y. An error if it is beyond the palette's size limits or no turn
// of the panel suits `frame`.
std::expected<z13::station::Block, std::string> PrismBlock(
    const PrismFrame& frame, std::span<const PrismCorner> corners, const Palette& palette);

}  // namespace z13::building::primitives
