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

#include <functional>

#include <Eigen/Dense>
#include <flecs.h>

#include <z13/components/station/block.h>
#include <z13/components/station/block_brush.h>
#include <z13/components/station/building.h>

#include "block_meshes.h"
#include "render_components.h"

namespace z13::raylib {

using BrushQuery = flecs::query<const z13::building::Brush, const Eigen::Matrix4f>;

// Brush previews, drawn one by one past the chunks.
struct SingleBlockDrawing {
  std::reference_wrapper<BlockMeshes> meshes;
  std::reference_wrapper<const Lighting> lighting;
  BlockMeshes::OptionalPalette palette;
};

// What a build would place: in station mode the brush's BrushPreview, green if it would
// be accepted, else the ship scene's cube, snapped around the brush as the build snaps it.
void DrawBrushPreview(
    const flecs::world& world, flecs::entity brush, const Eigen::Matrix4f& transform,
    const SingleBlockDrawing& drawing);

// The room overlay (F4) and the layer a module publishes (F6), as wireframes.
void DrawRoomOverlay(const flecs::world& world);
void DrawLayerOverlay(const flecs::world& world);

// The lines the brushes' previews carry (a wall's stick and the arrow along its axis).
void DrawBrushLines(const BrushQuery& brushes);

}  // namespace z13::raylib
