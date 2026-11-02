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

#include <chrono>
#include <optional>

#include <flecs.h>

#include <lib_core/utils/drawn_poses.h>
#include <lib_core/utils/pose_smoothing.h>
#include <rooms/view_culling.h>
#include <z13_settings/settings.h>

#include "scene_queries.h"
#include "scene_view.h"

namespace z13::raylib {

// Everything not the local player's own chases its simulated transform in real time, so
// rollback corrections glide in.
struct SmoothedDrawing {
  z13::DrawnPoses poses;
  std::optional<std::chrono::steady_clock::time_point> last_frame;
  std::optional<std::chrono::steady_clock::time_point> last_avatar_log;
};

void ChaseDrawnPoses(SmoothedDrawing& drawing, const VisualSmoothing& settings, const DrawnQuery& drawn_query);

// Every avatar shares the one model; only its transform and tint change per player.
void DrawRemotePlayers(const flecs::world& world, const RemotePlayerQuery& remote_players, const GhostQuery& ghosts,
                       const z13::DrawnPoses& drawn, const z13::station::rooms::ViewCulling& culling,
                       float avatar_radius);

// Debug: what DrawRemotePlayers decided for each remote avatar, every few seconds.
void LogRemotePlayers(const flecs::world& world, const RemotePlayerQuery& remote_players, SmoothedDrawing& drawing,
                      const SceneView& view, float avatar_radius);

}  // namespace z13::raylib
