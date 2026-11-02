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

#include <Eigen/Dense>
#include <flecs.h>
#include <raylib.h>

#include <lib_core/utils/drawn_poses.h>
#include <rooms/view_culling.h>
#include <z13_settings/settings.h>

#include "cell_lighting.h"
#include "block_chunks.h"
#include "light_clusters.h"
#include "lights.h"
#include "render_components.h"
#include "scene_queries.h"
#include "shadow_maps.h"

namespace z13::raylib {

void SetViewPosition(const flecs::world& world, const ::Vector3& eye);

// Every player as its avatar, the local one too: it is drawn only in shadows.
std::vector<MovingCaster> PlayerCasters(const DrawnQuery& players, const z13::DrawnPoses& drawn, float avatar_radius);

ShadowCasters CastersOf(const BlockChunks& chunks, const AvatarModel* avatar, std::vector<MovingCaster> players);

// The flashlights switched on, from each player's eye along its look, nearest first.
std::vector<FrameLight> FlashlightsOf(const FlashlightQuery& flashlights, const z13::DrawnPoses& drawn,
                                      const z13::station::rooms::ViewCulling& culling, const Eigen::Vector3f& eye,
                                      const z13::RenderTuning& tuning);

struct LitScene {
  // The flashlights, then the lamps players cast shadows from.
  std::vector<FrameLight> lights;
  SceneLight scene;
  ShadowAtlasLayout atlas;
};

// A station is lit by its lamps' baked light, and by the sun and the fill only where the hull
// lets them in. Flashlights come first, so they get their shadows before any lamp.
LitScene LightScene(const flecs::world& world, std::vector<FrameLight> flashlights, const CellLighting* cell_lighting,
                    ShadowMaps* shadows, const ShadowCasters& casters, const z13::RenderTuning& tuning);

// As BeginScene3D set it up.
ClusterView CurrentClusterView(const Eigen::Vector2i& size);

void UploadLighting(const flecs::world& world, const LitScene& lit, const ShadowMaps* shadows,
                    const ClusterView& view, const z13::RenderTuning& tuning);

}  // namespace z13::raylib
