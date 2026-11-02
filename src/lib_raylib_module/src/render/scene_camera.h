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

#include <flecs.h>
#include <raylib.h>

#include <Eigen/Dense>

#include <z13/components/gameplay/gameplay.h>
#include <z13/components/gameplay/player_parts.h>

#include <raylib_module/raylib_components.h>

#include "render_components.h"

namespace z13::raylib {

// Cameras are derived from gameplay::Camera components each frame, not from add
// events, so restored or edited state is picked up too.
void EnsureRaylibCamera(flecs::entity e, const RaylibData&, const gameplay::Camera& camera);

void SyncRaylibCamera(RaylibCamera& raylib_camera, const gameplay::Camera& camera, const Eigen::Matrix4f& transform);

// Only the local player's camera renders; other players' Cameras arrive with snapshots.
void ReleaseOrphanRaylibCamera(flecs::entity e, const RaylibCamera&);

// The screen spans 2 in normalized device coordinates along each axis.
Eigen::Vector2f PixelsInNdc(float pixels, const Eigen::Vector2i& screen);

// The view-projection rlgl draws with, set by BeginScene3D.
Eigen::Matrix4f CurrentViewProjection();

// Replacement for BeginMode3D/EndMode3D: those need CORE for the aspect ratio,
// which does not exist without InitWindow. Feed rlgl's matrices directly.
void BeginScene3D(const ::Camera3D& camera, const Eigen::Vector2i& size);

void EndScene3D();

}  // namespace z13::raylib
