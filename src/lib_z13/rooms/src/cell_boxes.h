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

#include <cstdint>

#include <primitives/placement.h>
#include <rooms/free_space.h>

namespace z13::station::rooms {

int64_t Volume(const z13::building::primitives::CellBox& box);
// Whether two boxes with disjoint insides share part of a face.
bool ShareFace(const z13::building::primitives::CellBox& first, const z13::building::primitives::CellBox& second);
LeafBox ToLeafBox(const z13::building::primitives::CellBox& box);

}  // namespace z13::station::rooms
