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

#include "shadow_maps_state.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>
#include <string_view>
#include <utility>

#include <raymath.h>
#include <rlgl.h>

#include <lib_core/utils/frustum.h>

#include "../platform/sdl_platform.h"
#include "../tools/math_convert.h"
#include "render_resources.h"

namespace z13::raylib {

namespace {

// Uniform names in assets/shaders/shadow.fs.
constexpr std::string_view kLightPositionUniform = "lightPosition";
constexpr std::string_view kLightRadiusUniform = "lightRadius";

constexpr int kCubeFaces = 6;
constexpr uint8_t kAllFaces = (1 << kCubeFaces) - 1;
constexpr float kCubeFovDeg = 90.f;
constexpr float kCubeNear = 0.05f;
// A directional map's eye stands this many of the station's radii back from its centre; its
// depth range spans the station with a radius to spare on both sides.
constexpr float kDirectionalDistanceRadii = 2.f;
constexpr float kDirectionalMarginM = 1.f;
constexpr float kNearlyVertical = 0.99f;
// A cleared tile is as far as the light reaches, the shader's normalized distance 1.
constexpr unsigned char kFarthest = 255;

struct CubeFace {
  Eigen::Vector3f forward;
  Eigen::Vector3f up;
};

// OpenGL's cube face order and orientation; assets/shaders/lighting.fs samples them so.
const std::array kCubeFaceViews {
    CubeFace {Eigen::Vector3f::UnitX(), -Eigen::Vector3f::UnitY()},
    CubeFace {-Eigen::Vector3f::UnitX(), -Eigen::Vector3f::UnitY()},
    CubeFace {Eigen::Vector3f::UnitY(), Eigen::Vector3f::UnitZ()},
    CubeFace {-Eigen::Vector3f::UnitY(), -Eigen::Vector3f::UnitZ()},
    CubeFace {Eigen::Vector3f::UnitZ(), -Eigen::Vector3f::UnitY()},
    CubeFace {-Eigen::Vector3f::UnitZ(), -Eigen::Vector3f::UnitY()},
};
static_assert(kCubeFaceViews.size() == kCubeFaces);

bool Overlap(const Eigen::AlignedBox3f& a, const Eigen::AlignedBox3f& b) {
  return !a.intersection(b).isEmpty();
}

uint8_t FaceBit(const Eigen::Vector3f& direction) {
  Eigen::Index axis = 0;
  direction.cwiseAbs().maxCoeff(&axis);
  const int face = (static_cast<int>(axis) * 2) + (direction[axis] > 0.f ? 0 : 1);
  return static_cast<uint8_t>(1 << face);
}

// The faces of a cube at `light` that see `box`, by its corners and centre; all of them for a
// box about as near as it is large, whose corners don't bound what it covers.
uint8_t FacesSeeing(const Eigen::AlignedBox3f& box, const Eigen::Vector3f& light) {
  if (box.exteriorDistance(light) <= box.sizes().maxCoeff()) {
    return kAllFaces;
  }
  uint8_t faces = FaceBit(box.center() - light);
  for (int corner = 0; corner < (1 << 3); ++corner) {
    faces |= FaceBit(box.corner(static_cast<Eigen::AlignedBox3f::CornerType>(corner)) - light);
  }
  return faces;
}

// The faces a spot's cone reaches into, by its axis and rim; others are never sampled.
uint8_t FacesInCone(const SpotCone& cone) {
  constexpr int kRimSamples = 8;
  const Eigen::Vector3f side = cone.direction.unitOrthogonal();
  const Eigen::Vector3f other_side = cone.direction.cross(side);
  const float spread = std::tan(std::acos(cone.cos_half_angle));
  uint8_t faces = FaceBit(cone.direction);
  for (int i = 0; i < kRimSamples; ++i) {
    const float angle = 2.f * std::numbers::pi_v<float> * static_cast<float>(i) / kRimSamples;
    faces |= FaceBit(cone.direction + (spread * ((std::cos(angle) * side) + (std::sin(angle) * other_side))));
  }
  return faces;
}

bool SameLight(const SlotView& drawn, const FrameLight& light) {
  return light.owner ? drawn.owner == light.owner : (!drawn.owner && drawn.light == light.position);
}

Eigen::Vector3f DirectionOf(const FrameLight& light) {
  return light.cone ? light.cone->direction : Eigen::Vector3f::Zero();
}

// Shadow passes write a distance or depth, not a color to blend, and see both faces of a wall.
void BeginShadowPass(unsigned int framebuffer) {
  rlDrawRenderBatchActive();
  rlDisableColorBlend();
  rlDisableBackfaceCulling();
  rlEnableDepthTest();
  rlEnableFramebuffer(framebuffer);
  rlClearColor(kFarthest, kFarthest, kFarthest, kFarthest);
}

void EndShadowPass() {
  rlDrawRenderBatchActive();
  rlDisableFramebuffer();
  rlEnableBackfaceCulling();
  rlEnableColorBlend();
}

// Where `box` falls on a directional map, in its normalized device coordinates.
Eigen::AlignedBox2f MapRect(const Eigen::Matrix4f& view_projection, const Eigen::AlignedBox3f& box) {
  Eigen::AlignedBox2f rect;
  for (int corner = 0; corner < (1 << 3); ++corner) {
    const Eigen::Vector4f point = view_projection *
        box.corner(static_cast<Eigen::AlignedBox3f::CornerType>(corner)).homogeneous();
    rect.extend(point.head<2>() / point.w());
  }
  return rect;
}

// `rect` in a map's pixels, a pixel wider on each side for the texels it only touches.
Eigen::AlignedBox2i PixelRect(const Eigen::AlignedBox2f& rect, int size) {
  const auto to_pixels = [size](const Eigen::Vector2f& ndc) { return (ndc.array() + 1.f) * (size / 2.f); };
  const Eigen::Vector2i low = to_pixels(rect.min()).floor().cast<int>().matrix() - Eigen::Vector2i::Ones();
  const Eigen::Vector2i high = to_pixels(rect.max()).ceil().cast<int>().matrix() + Eigen::Vector2i::Ones();
  return {low.cwiseMax(0), high.cwiseMin(size)};
}

Eigen::Vector2i TileCorner(const ShadowAtlasLayout& layout, size_t slot, int face) {
  const int tile = (static_cast<int>(slot) * kCubeFaces) + face;
  return Eigen::Vector2i(tile % layout.columns, tile / layout.columns) * layout.tile_size;
}

}  // namespace

ShadowMaps::State::State(::Shader shadow_shader) : shader_(MakeManagedShader(shadow_shader)) {
  light_position_location_ = GetShaderLocation(shadow_shader, kLightPositionUniform.data());
  light_radius_location_ = GetShaderLocation(shadow_shader, kLightRadiusUniform.data());
}

ShadowMaps::State::~State() {
  if (!IsGlContextAlive()) {
    return;
  }
  ReleaseAtlas();
  for (const DirectionalShadow& shadow : directional_) {
    if (shadow.framebuffer != 0) {
      rlUnloadFramebuffer(shadow.framebuffer);
    }
  }
}

::Material ShadowMaps::State::Material() {
  ::Material material {};
  material.shader = *shader_;
  material.maps = maps_.data();
  return material;
}

void ShadowMaps::State::ReleaseAtlas() {
  // Unloading the framebuffer frees its depth buffer too.
  if (atlas_framebuffer_ != 0) {
    rlUnloadFramebuffer(atlas_framebuffer_);
  }
  if (atlas_texture_ != 0) {
    rlUnloadTexture(atlas_texture_);
  }
  atlas_framebuffer_ = 0;
  atlas_texture_ = 0;
  layout_ = {};
  slots_ = {};
}

Status ShadowMaps::State::ResizeAtlas(int slots, int tile_size) {
  if (static_cast<int>(slots_.size()) == slots && layout_.tile_size == tile_size) {
    return {};
  }
  if (failed_atlas_ == Eigen::Vector2i(slots, tile_size)) {
    return {};
  }
  ReleaseAtlas();
  failed_atlas_.reset();
  if (slots == 0) {
    return {};
  }
  const int tiles = slots * kCubeFaces;
  const int columns = static_cast<int>(std::ceil(std::sqrt(static_cast<float>(tiles))));
  const Eigen::Vector2i size = Eigen::Vector2i(columns, (tiles + columns - 1) / columns) * tile_size;
  atlas_texture_ = rlLoadTexture(nullptr, size.x(), size.y(), RL_PIXELFORMAT_UNCOMPRESSED_R32, 1);
  atlas_framebuffer_ = rlLoadFramebuffer();
  rlFramebufferAttach(atlas_framebuffer_, rlLoadTextureDepth(size.x(), size.y(), true), RL_ATTACHMENT_DEPTH,
                      RL_ATTACHMENT_RENDERBUFFER, 0);
  rlFramebufferAttach(atlas_framebuffer_, atlas_texture_, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
  if (atlas_texture_ == 0 || !rlFramebufferComplete(atlas_framebuffer_)) {
    ReleaseAtlas();
    failed_atlas_ = Eigen::Vector2i(slots, tile_size);
    return std::unexpected(std::format("can't make a shadow atlas of {} x {} px", size.x(), size.y()));
  }
  layout_ = {.tile_size = tile_size, .columns = columns};
  slots_.resize(static_cast<size_t>(slots));
  return {};
}

std::vector<std::optional<size_t>> ShadowMaps::State::AssignSlots(
    std::span<const FrameLight> lights, int new_slots) const {
  std::vector<std::optional<size_t>> assigned(lights.size());
  std::vector<bool> taken(slots_.size());
  for (size_t i = 0; i < lights.size(); ++i) {
    for (size_t slot = 0; slot < slots_.size(); ++slot) {
      if (!taken[slot] && slots_[slot] && SameLight(*slots_[slot], lights[i])) {
        assigned[i] = slot;
        taken[slot] = true;
        break;
      }
    }
  }
  // Empty slots first, then those of lamps gone from view.
  std::vector<size_t> free;
  for (size_t slot = 0; slot < slots_.size(); ++slot) {
    if (!taken[slot] && !slots_[slot]) {
      free.push_back(slot);
    }
  }
  for (size_t slot = 0; slot < slots_.size(); ++slot) {
    if (!taken[slot] && slots_[slot]) {
      free.push_back(slot);
    }
  }
  auto next = free.begin();
  for (std::optional<size_t>& slot : assigned) {
    if (!slot && new_slots > 0 && next != free.end()) {
      slot = *next++;
      --new_slots;
    }
  }
  return assigned;
}

void ShadowMaps::State::DrawFaces(size_t slot, const FrameLight& light, const ShadowCasters& casters, uint8_t faces) {
  BeginShadowPass(atlas_framebuffer_);
  rlEnableScissorTest();
  SetShaderValue(*shader_, light_position_location_, light.position.data(), SHADER_UNIFORM_VEC3);
  SetShaderValue(*shader_, light_radius_location_, &light.radius, SHADER_UNIFORM_FLOAT);
  const ::Material material = Material();
  const ::Vector3 eye = EigenToRaylibVector(light.position);
  const ::Matrix projection = MatrixPerspective(kCubeFovDeg * DEG2RAD, 1.f, kCubeNear, light.radius);
  for (int face = 0; face < kCubeFaces; ++face) {
    if ((faces & (1 << face)) == 0) {
      continue;
    }
    const Eigen::Vector2i corner = TileCorner(layout_, slot, face);
    rlViewport(corner.x(), corner.y(), layout_.tile_size, layout_.tile_size);
    rlScissor(corner.x(), corner.y(), layout_.tile_size, layout_.tile_size);
    rlClearScreenBuffers();
    const CubeFace& view = kCubeFaceViews[static_cast<size_t>(face)];
    const ::Matrix look = MatrixLookAt(eye, EigenToRaylibVector(light.position + view.forward), EigenToRaylibVector(view.up));
    rlSetMatrixProjection(projection);
    rlSetMatrixModelview(look);
    const z13::math::Frustum frustum(RaylibToEigenMatrix(projection) * RaylibToEigenMatrix(look));
    casters.draw_static(
        [&](const Eigen::AlignedBox3f& bounds) {
          return Overlap(bounds, light.radius_box) && frustum.Intersects(bounds);
        },
        material);
    for (const MovingCaster& caster : casters.moving) {
      if (caster.entity != light.owner && Overlap(caster.box, light.radius_box) && frustum.Intersects(caster.box)) {
        casters.draw_moving(caster, material);
      }
    }
    rlDrawRenderBatchActive();
  }
  rlDisableScissorTest();
  EndShadowPass();
}

Status ShadowMaps::State::UpdateLamps(
    std::span<FrameLight> lights, const ShadowCasters& casters, const z13::RenderTuning& tuning) {
  Status resized = ResizeAtlas(std::clamp(tuning.shadow_lights, 0, kMaxLights), tuning.shadow_map_size);
  const std::span<FrameLight> candidates = lights.first(std::min(lights.size(), slots_.size()));
  const std::vector<std::optional<size_t>> assigned = AssignSlots(candidates, tuning.shadow_updates_per_frame);
  int redraws_left = tuning.shadow_updates_per_frame;
  for (size_t i = 0; i < candidates.size(); ++i) {
    if (assigned[i] && !(slots_[*assigned[i]] && SameLight(*slots_[*assigned[i]], candidates[i]))) {
      --redraws_left;
    }
  }
  for (size_t i = 0; i < candidates.size(); ++i) {
    if (!assigned[i]) {
      continue;
    }
    FrameLight& light = candidates[i];
    std::optional<SlotView>& drawn = slots_[*assigned[i]];
    const uint8_t sampled = light.cone ? FacesInCone(*light.cone) : kAllFaces;
    uint8_t moving_faces {};
    for (const MovingCaster& caster : casters.moving) {
      if (caster.entity != light.owner && Overlap(caster.box, light.radius_box)) {
        moving_faces |= FacesSeeing(caster.box, light.position);
      }
    }
    const bool same_view = drawn && SameLight(*drawn, light) && drawn->light == light.position &&
                           drawn->direction == DirectionOf(light) && drawn->radius == light.radius;
    const auto [redrawn, static_version] =
        same_view ? FacesToRedraw(*drawn, light, casters, moving_faces, redraws_left)
                  : std::pair(kAllFaces, casters.static_version);
    const uint8_t faces = sampled & redrawn;
    if (faces != 0) {
      DrawFaces(*assigned[i], light, casters, faces);
    }
    drawn = SlotView {
        .owner = light.owner,
        .light = light.position,
        .direction = DirectionOf(light),
        .radius = light.radius,
        .static_version = static_version,
        .moving_faces = moving_faces,
    };
    light.shadow_slot = static_cast<int>(*assigned[i]);
    light.reach = light.radius_box;
  }
  return resized;
}

std::pair<uint8_t, uint64_t> ShadowMaps::State::FacesToRedraw(
    const SlotView& drawn, const FrameLight& light, const ShadowCasters& casters, uint8_t moving_faces,
    int& redraws_left) const {
  const uint8_t faces = moving_faces | drawn.moving_faces;
  if (drawn.static_version == casters.static_version) {
    return {faces, drawn.static_version};
  }
  const Eigen::AlignedBox3f changed = casters.changed_since(drawn.static_version).intersection(light.radius_box);
  if (changed.isEmpty()) {
    return {faces, casters.static_version};
  }
  if (redraws_left <= 0) {
    return {faces, drawn.static_version};
  }
  --redraws_left;
  return {faces | FacesSeeing(changed, light.position), casters.static_version};
}

bool ShadowMaps::State::ResizeDirectional(DirectionalShadow& shadow, int size) {
  if (shadow.framebuffer != 0 && shadow.size == size) {
    return true;
  }
  if (shadow.failed_size == size) {
    return false;
  }
  if (shadow.framebuffer != 0) {
    rlUnloadFramebuffer(shadow.framebuffer);
  }
  shadow = {.size = size};
  shadow.framebuffer = rlLoadFramebuffer();
  shadow.texture = rlLoadTextureDepth(size, size, false);
  rlFramebufferAttach(shadow.framebuffer, shadow.texture, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_TEXTURE2D, 0);
  if (shadow.texture == 0 || !rlFramebufferComplete(shadow.framebuffer)) {
    rlUnloadFramebuffer(shadow.framebuffer);
    shadow = {.failed_size = size};
    return false;
  }
  return true;
}

std::optional<Eigen::Matrix4f> ShadowMaps::State::UpdateDirectional(
    DirectionalMap map, const Eigen::Vector3f& direction, const ShadowCasters& casters,
    const z13::RenderTuning& tuning) {
  if (!casters.static_bounds) {
    return std::nullopt;
  }
  DirectionalShadow& shadow = directional_[static_cast<size_t>(map)];
  const Eigen::AlignedBox3f& bounds = *casters.static_bounds;
  const DirectionalView view {
      .direction = direction,
      .static_version = casters.static_version,
      .bounds_min = bounds.min(),
      .bounds_max = bounds.max(),
      .size = tuning.sun_shadow_map_size,
  };
  if (!ResizeDirectional(shadow, view.size)) {
    return std::nullopt;
  }
  if (shadow.drawn == view) {
    return shadow.view_projection;
  }
  DirectionalView same_blocks = view;
  same_blocks.static_version = shadow.drawn ? shadow.drawn->static_version : 0;
  if (shadow.drawn == same_blocks) {
    const Eigen::AlignedBox3f changed = casters.changed_since(shadow.drawn->static_version);
    if (!changed.isEmpty()) {
      const Eigen::AlignedBox2f rect = MapRect(shadow.view_projection, changed);
      DrawDirectional(
          shadow, casters,
          [&](const Eigen::AlignedBox3f& bounds) {
            return !MapRect(shadow.view_projection, bounds).intersection(rect).isEmpty();
          },
          PixelRect(rect, view.size));
    }
    shadow.drawn = view;
    return shadow.view_projection;
  }

  const float radius = (bounds.diagonal().norm() / 2.f) + kDirectionalMarginM;
  const Eigen::Vector3f centre = bounds.center();
  const Eigen::Vector3f eye = centre - (direction * (kDirectionalDistanceRadii * radius));
  const Eigen::Vector3f up =
      std::abs(direction.z()) > kNearlyVertical ? Eigen::Vector3f::UnitY() : Eigen::Vector3f::UnitZ();
  const ::Matrix projection = MatrixOrtho(-radius, radius, -radius, radius, (kDirectionalDistanceRadii - 1.f) * radius,
                                          (kDirectionalDistanceRadii + 1.f) * radius);
  const ::Matrix look = MatrixLookAt(EigenToRaylibVector(eye), EigenToRaylibVector(centre), EigenToRaylibVector(up));
  shadow.view_projection = RaylibToEigenMatrix(projection) * RaylibToEigenMatrix(look);
  DrawDirectional(shadow, casters, [](const Eigen::AlignedBox3f&) { return true; }, std::nullopt);
  shadow.drawn = view;
  return shadow.view_projection;
}

void ShadowMaps::State::DrawDirectional(const DirectionalShadow& shadow, const ShadowCasters& casters,
                                        const CasterFilter& keep, std::optional<Eigen::AlignedBox2i> scissor) {
  BeginShadowPass(shadow.framebuffer);
  rlViewport(0, 0, shadow.size, shadow.size);
  if (scissor) {
    rlEnableScissorTest();
    rlScissor(scissor->min().x(), scissor->min().y(), scissor->sizes().x(), scissor->sizes().y());
  }
  rlClearScreenBuffers();
  // The whole transform as the projection: the shader takes only their product.
  rlSetMatrixProjection(EigenToRaylibMatrix(shadow.view_projection));
  rlSetMatrixModelview(MatrixIdentity());
  casters.draw_static(keep, Material());
  if (scissor) {
    rlDrawRenderBatchActive();
    rlDisableScissorTest();
  }
  EndShadowPass();
}

void ShadowMaps::State::Bind() const {
  constexpr std::array kUnits {kSunShadowUnit, kFillShadowUnit};
  for (size_t map = 0; map < directional_.size(); ++map) {
    if (directional_[map].drawn) {
      rlActiveTextureSlot(kUnits[map]);
      rlEnableTexture(directional_[map].texture);
    }
  }
  if (atlas_texture_ != 0) {
    rlActiveTextureSlot(kShadowAtlasUnit);
    rlEnableTexture(atlas_texture_);
  }
  rlActiveTextureSlot(0);
}

}  // namespace z13::raylib
