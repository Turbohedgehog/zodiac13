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

#include <vector>

#include <z13/components/station.h>
#include <z13_primitives/palette.h>
#include <z13_primitives/placement.h>

namespace z13::building::primitives {

// What is left of `block` once the cells of `cut` are taken out of it: blocks of the same
// type and turn that tile the rest, up to six, split along X, then Y, then Z. A block that
// can't be split (a fixed-size primitive, or a rest below the size limits) leaves nothing.
// The order is fixed, so every peer ends up with the same blocks.
std::vector<z13::station::Block> LeftAfterCut(
    const z13::station::Block& block, const Primitive& primitive, const CellBox& cut);

}  // namespace z13::building::primitives
