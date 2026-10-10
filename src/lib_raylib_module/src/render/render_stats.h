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

#include <cstddef>
#include <optional>

namespace z13::raylib {

// CPU time of each part of the scene's drawing, in ms; a wait for the GPU lands in the part
// that waited.
struct DrawTimes {
  // The skybox, the spaceship model and the lighting uniforms.
  double background_ms {};
  double players_ms {};
  double opaque_ms {};
  double glass_ms {};
  double previews_ms {};
  // The checker shader's reset and the F4 room overlay.
  double overlay_ms {};
  // Unloading the block meshes nothing drew.
  double release_ms {};
  double flush_ms {};

  DrawTimes& operator+=(const DrawTimes& other) {
    background_ms += other.background_ms;
    players_ms += other.players_ms;
    opaque_ms += other.opaque_ms;
    glass_ms += other.glass_ms;
    previews_ms += other.previews_ms;
    overlay_ms += other.overlay_ms;
    release_ms += other.release_ms;
    flush_ms += other.flush_ms;
    return *this;
  }
};

// What the last frame drew, for the stats overlay; never state.
struct RenderStats {
  using Singleton = void;
  size_t chunks {};
  size_t chunks_drawn {};
  size_t meshes_drawn {};
  size_t glass {};
  size_t glass_drawn {};
  // Nothing while rooms don't cull.
  std::optional<size_t> rooms_seen;
  // Walking the portals and narrowing the frustum.
  double culling_us {};
  DrawTimes times {};
};

}  // namespace z13::raylib
