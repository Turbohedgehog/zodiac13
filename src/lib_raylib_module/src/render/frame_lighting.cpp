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

#include "frame_lighting.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <utility>

#include <raymath.h>
#include <rlgl.h>

#include <lib_core/utils/log.h>
#include <lib_core/utils/math.h>
#include <lib_core/utils/status.h>
#include <z13/components/station/block.h>
#include <z13/components/utility/lamps.h>
#include <z13/components/gameplay/player_parts.h>
#include <z13/components/station/station_mode.h>
#include <cell_light/lamp.h>

#include "../tools/math_convert.h"
#include "block_chunks.h"

namespace z13::raylib {

namespace {

// Bluish, as light from the sky around would be; scaled by render.outside_fill.
const Eigen::Vector3f kFillTint(0.8f, 0.87f, 1.f);
// Warm, as an LED torch; scaled by render.flashlight_intensity.
const Eigen::Vector3f kFlashlightTint(1.f, 0.95f, 0.85f);
// Keeps the ship scene's faces the sun misses from going black.
const Eigen::Vector3f kShipAmbientSky(0.30f, 0.32f, 0.40f);
const Eigen::Vector3f kShipAmbientGround(0.12f, 0.10f, 0.09f);

const Eigen::Vector3f kSunColor = Eigen::Vector3f::Ones();

Eigen::Vector3f SunDirection(const z13::RenderTuning& tuning) {
  return Eigen::Vector3f(tuning.sun_direction_x, tuning.sun_direction_y, tuning.sun_direction_z).normalized();
}

// The lamps whose radius holds a player, nearest a player first, at most `count`. Per
// component: this runs over every lamp each frame.
std::vector<FrameLight> PlayerShadowLamps(const CellLighting& lighting, std::span<const MovingCaster> players,
                                          size_t count) {
  std::vector<Eigen::Vector3f> centres;
  std::ranges::transform(players, std::back_inserter(centres),
                         [](const MovingCaster& player) { return player.box.center(); });
  const float cell = z13::station::kCellSize;
  std::vector<std::pair<float, size_t>> near;
  const std::vector<z13::station::light::Lamp>& lamps = lighting.ShiningLamps();
  for (size_t i = 0; i < lamps.size(); ++i) {
    const Eigen::Vector3f& position = lamps[i].position;
    const float radius = lamps[i].radius * cell;
    float nearest = radius * radius;
    for (const Eigen::Vector3f& centre : centres) {
      const float dx = (position.x() * cell) - centre.x();
      const float dy = (position.y() * cell) - centre.y();
      const float dz = (position.z() * cell) - centre.z();
      nearest = std::min(nearest, (dx * dx) + (dy * dy) + (dz * dz));
    }
    if (nearest < radius * radius) {
      near.emplace_back(nearest, i);
    }
  }
  std::ranges::sort(near);
  std::vector<FrameLight> chosen;
  for (size_t i = 0; i < std::min(count, near.size()); ++i) {
    const z13::station::light::Lamp& lamp = lamps[near[i].second];
    const Eigen::Vector3f position = lamp.position * cell;
    const Eigen::Vector3f reach = Eigen::Vector3f::Constant(lamp.radius * cell);
    const Eigen::AlignedBox3f radius_box(position - reach, position + reach);
    chosen.push_back({.position = position,
                      .radius = lamp.radius * cell,
                      .color = lamp.color,
                      .reach = radius_box,
                      .radius_box = radius_box,
                      .use = LightUse::kPlayerShadows});
  }
  return chosen;
}

}  // namespace

void SetViewPosition(const flecs::world& world, const ::Vector3& eye) {
  if (!world.has<Lighting>()) {
    return;
  }
  const Lighting& lighting = world.get<Lighting>();
  if (lighting.res) {
    const std::array view_pos = {eye.x, eye.y, eye.z};
    SetShaderValue(*lighting.res->shader, lighting.res->shader->locs[SHADER_LOC_VECTOR_VIEW], view_pos.data(),
                   SHADER_UNIFORM_VEC3);
  }
}

std::vector<MovingCaster> PlayerCasters(const DrawnQuery& players, const z13::DrawnPoses& drawn, float avatar_radius) {
  const Eigen::Vector3f reach = Eigen::Vector3f::Constant(avatar_radius);
  std::vector<MovingCaster> casters;
  players.each([&](flecs::entity e, const Eigen::Matrix4f& transform) {
    const Eigen::Matrix4f drawn_transform = z13::DrawnTransform(drawn, e, transform);
    const Eigen::Vector3f centre = z13::math::ExtractTranslation<float>(drawn_transform);
    casters.push_back({.entity = e.id(), .transform = drawn_transform, .box = {centre - reach, centre + reach}});
  });
  return casters;
}

ShadowCasters CastersOf(const BlockChunks& chunks, const AvatarModel* avatar, std::vector<MovingCaster> players) {
  ShadowCasters casters {
      .draw_static = [&chunks](const CasterFilter& filter,
                               const ::Material& material) { chunks.DrawShadowCasters(filter, material); },
      .static_version = chunks.Version(),
      .changed_since = [&chunks](uint64_t version) { return chunks.ChangedSince(version); },
      .static_bounds = chunks.OpaqueBounds(),
      .draw_moving = [](const MovingCaster&, const ::Material&) {},
  };
  if (avatar == nullptr || !avatar->res || avatar->res->model->meshCount == 0) {
    return casters;
  }
  const ::Mesh& mesh = avatar->res->model->meshes[0];
  casters.draw_moving = [&mesh](const MovingCaster& caster, const ::Material& material) {
    DrawMesh(mesh, material, EigenToRaylibMatrix(caster.transform));
  };
  casters.moving = std::move(players);
  return casters;
}

std::vector<FrameLight> FlashlightsOf(const FlashlightQuery& flashlights, const z13::DrawnPoses& drawn,
                                      const z13::station::rooms::ViewCulling& culling, const Eigen::Vector3f& eye,
                                      const z13::RenderTuning& tuning) {
  const float range = tuning.flashlight_range_m;
  const float cos_half_angle = std::cos(z13::math::ToRadians(tuning.flashlight_half_angle_deg));
  std::vector<FrameLight> lights;
  flashlights.each([&](flecs::entity e, const gameplay::Flashlight& flashlight, const Eigen::Matrix4f& transform) {
    if (!flashlight.on) {
      return;
    }
    const Eigen::Matrix4f drawn_transform = z13::DrawnTransform(drawn, e, transform);
    const Eigen::Vector3f position = z13::math::ExtractTranslation<float>(drawn_transform);
    const Eigen::AlignedBox3f radius_box(position - Eigen::Vector3f::Constant(range),
                                         position + Eigen::Vector3f::Constant(range));
    if (!culling.Visible(radius_box)) {
      return;
    }
    lights.push_back({
        .position = position,
        .radius = range,
        .color = kFlashlightTint * tuning.flashlight_intensity,
        .reach = radius_box,
        .radius_box = radius_box,
        .cone = SpotCone {.direction = drawn_transform.block<3, 1>(0, 0).normalized(), .cos_half_angle = cos_half_angle},
        .owner = e.id(),
    });
  });
  std::ranges::sort(lights, {}, [&eye](const FrameLight& light) { return (light.position - eye).squaredNorm(); });
  return lights;
}

LitScene LightScene(const flecs::world& world, std::vector<FrameLight> flashlights, const CellLighting* cell_lighting,
                    ShadowMaps* shadows, const ShadowCasters& casters, const z13::RenderTuning& tuning) {
  const Eigen::Vector3f sun_direction = SunDirection(tuning);
  LitScene lit {.lights = std::move(flashlights)};
  lit.lights.resize(std::min(lit.lights.size(), static_cast<size_t>(tuning.frame_lights)));
  if (!world.has<z13::station::StationMode>()) {
    lit.scene.sun = DirectionalLight {.direction = sun_direction, .color = kSunColor};
    lit.scene.ambient_sky = kShipAmbientSky;
    lit.scene.ambient_ground = kShipAmbientGround;
    return lit;
  }
  const auto shadow_lights = static_cast<size_t>(tuning.shadow_lights);
  if (cell_lighting != nullptr && !casters.moving.empty() && lit.lights.size() < shadow_lights) {
    std::ranges::move(PlayerShadowLamps(*cell_lighting, casters.moving, shadow_lights - lit.lights.size()),
                      std::back_inserter(lit.lights));
  }
  // Without the hull's shadows the sun and the fill would light the rooms too.
  if (shadows == nullptr) {
    return lit;
  }
  if (const Status lamps = shadows->UpdateLamps(lit.lights, casters, tuning); !lamps) {
    log_error("[raylib] shadows: {}", lamps.error());
  }
  lit.atlas = shadows->AtlasLayout();
  if (const auto sun_shadow = shadows->UpdateDirectional(DirectionalMap::kSun, sun_direction, casters, tuning)) {
    lit.scene.sun =
        DirectionalLight {.direction = sun_direction, .color = kSunColor, .shadow_view_projection = sun_shadow};
  }
  if (const auto fill_shadow = shadows->UpdateDirectional(DirectionalMap::kFill, -sun_direction, casters, tuning)) {
    lit.scene.fill = DirectionalLight {
        .direction = -sun_direction, .color = kFillTint * tuning.outside_fill, .shadow_view_projection = fill_shadow};
  }
  return lit;
}

ClusterView CurrentClusterView(const Eigen::Vector2i& size) {
  const Eigen::Matrix4f view = RaylibToEigenMatrix(rlGetMatrixModelview());
  const ::Matrix projection = rlGetMatrixProjection();
  return {
      .view = view,
      .focal = {projection.m0, projection.m5},
      // The view looks down its -Z.
      .forward = -view.block<1, 3>(2, 0).transpose(),
      .screen = size,
      .near = static_cast<float>(rlGetCullDistanceNear()),
      .far = static_cast<float>(rlGetCullDistanceFar()),
  };
}

void UploadLighting(const flecs::world& world, const LitScene& lit, const ShadowMaps* shadows,
                    const ClusterView& view, const z13::RenderTuning& tuning) {
  if (!world.has<Lighting>()) {
    return;
  }
  const Lighting& lighting = world.get<Lighting>();
  if (!lighting.res) {
    return;
  }
  std::vector<FrameLight> lit_lights;
  std::ranges::copy_if(lit.lights, std::back_inserter(lit_lights),
                       [](const FrameLight& light) { return light.use == LightUse::kLights; });
  lighting.res->clusters.Update(lit_lights, view, tuning);
  lighting.res->clusters.Upload(*lighting.res->shader, lighting.res->lights);
  lighting.res->clusters.Bind();
  UploadShadowAtlas(*lighting.res->shader, lighting.res->lights, lit.atlas);
  UploadSceneLight(*lighting.res->shader, lighting.res->lights, lit.scene);
  UploadPlayerShadows(*lighting.res->shader, lighting.res->lights, lit.lights);
  if (shadows != nullptr) {
    shadows->Bind();
  }
  const auto* cell_lighting = world.try_get<CellLighting>();
  const int cell_light_off = 0;
  if (cell_lighting != nullptr && world.has<z13::station::StationMode>()) {
    cell_lighting->Upload(*lighting.res->shader, lighting.res->lights, Eigen::Matrix4f::Identity());
    cell_lighting->Bind();
  } else {
    SetShaderValue(*lighting.res->shader, lighting.res->lights.cell_light_enabled, &cell_light_off,
                   SHADER_UNIFORM_INT);
  }
}

}  // namespace z13::raylib
