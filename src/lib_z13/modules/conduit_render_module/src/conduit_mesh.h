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

#include <z13/components/conduit.h>
#include <z13/components/scene_mesh.h>

namespace z13::conduit_render {

// The box a run fills: its whole cells along its axis, `kind.thickness_cells` square across, centred in the cells.
// The revision is left for the caller.
z13::render::SceneMesh BuildConduitMesh(const z13::conduit::Conduit& conduit, const z13::conduit::ConduitKind& kind);

}  // namespace z13::conduit_render
