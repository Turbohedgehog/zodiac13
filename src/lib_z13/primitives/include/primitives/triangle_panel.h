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

#include <array>
#include <expected>
#include <optional>
#include <string>

#include <Eigen/Core>

#include <z13/components/block.h>
#include <primitives/palette.h>

namespace z13::building::primitives {

using Triangle = std::array<Eigen::Vector3i, 3>;

// The axis (0 to 2) a triangular wall thickens along: the one nearest the triangle's normal, the
// lower on a tie. Nothing for corners on one line.
std::optional<int> ThickeningAxisOf(const Triangle& triangle);

// The thinnest triangular wall that holds air back: a panel's rule (palette Panel's AllowsSize)
// over both slopes, thickness * |n| >= |n| + |p| + |q|, n the normal's component along the
// thickening axis, p and q the others. Nothing for corners on one line.
std::optional<int> MinTriangleThickness(const Triangle& triangle);

// The Panel block of the triangle, thickened by |thickness| cells along ThickeningAxisOf, toward
// the positive side if `thickness` is. An error for corners on one line, a thickness of none or
// under the least that holds air back, or a size the palette doesn't allow.
std::expected<z13::station::Block, std::string> TrianglePanelBlock(
    const Triangle& triangle, int thickness, const Palette& palette);

}  // namespace z13::building::primitives
