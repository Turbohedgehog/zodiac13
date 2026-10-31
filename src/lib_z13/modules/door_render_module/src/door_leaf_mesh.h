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

#include <optional>

#include <z13/components/block.h>
#include <z13/components/scene_mesh.h>
#include <primitives/geometry.h>

namespace z13::door_render {

// The leaf of a door block where its position puts it: rolled up into the top of the opening, so it shrinks from
// below until nothing is left at kDoorOpen. In world meters, coloured like the primitive's material; nothing for a
// block without a leaf or a fully open one. The revision is left for the caller.
std::optional<z13::render::SceneMesh> BuildLeafMesh(
    const z13::station::Block& block, int position, z13::building::primitives::OptionalPalette palette);

}  // namespace z13::door_render
