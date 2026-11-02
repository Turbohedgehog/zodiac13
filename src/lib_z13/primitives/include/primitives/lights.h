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
#include <vector>

#include <Eigen/Dense>

#include <z13/components/station/block.h>
#include <z13/components/gameplay/color.h>
#include <z13/components/utility/lamps.h>
#include <primitives/geometry.h>
#include <primitives/palette.h>

namespace z13::building::primitives {

// A lamp's light, in cells.
struct PointLight {
  Eigen::Vector3f position = Eigen::Vector3f::Zero();
  // As far past the opposite face: where it shines from when its bottom lies against a block
  // (a lamp on the floor).
  Eigen::Vector3f behind = Eigen::Vector3f::Zero();
  // The cell it shines from, to find its room.
  Eigen::Vector3i cell = Eigen::Vector3i::Zero();
  LightSource source;
};

// The lights of the blocks whose primitive has one, half a cell below each block's bottom
// face (its own -Z), so the block doesn't shadow its own light.
std::vector<PointLight> LightsOf(std::span<const z13::station::Block> blocks, OptionalPalette palette);

// What a dimmed lamp shines (z13/components/utility/lamps.h): the red of the emergency light.
inline constexpr z13::Rgba kEmergencyColor {255, 40, 30, 255};
inline constexpr int kPercent = 100;

// The same, with the lamps in `dimmed` (sorted by DimmedLampBefore) shining red at `emergency_percent` of their
// intensity.
std::vector<PointLight> LightsOf(std::span<const z13::station::Block> blocks, OptionalPalette palette,
                                 const std::vector<z13::utility::DimmedLamp>& dimmed, int emergency_percent);

}  // namespace z13::building::primitives
