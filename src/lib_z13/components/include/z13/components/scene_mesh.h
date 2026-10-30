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
#include <vector>

#include <Eigen/Dense>

#include <z13/components/color.h>

// The render API for modules that draw their own things (conduit_render_module): they put a SceneMesh on an
// entity, raylib_module uploads it and draws it with the scene's light. Nothing here is state.
namespace z13::render {

// Where modules derive their SceneMeshes: after the simulation of the frame, before raylib_module reads them.
struct SceneMeshPhase {};

// Flat-shaded triangles in world meters, drawn lit and opaque, culled by their bounds and casting no shadow.
struct SceneMesh {
  // Three per triangle, wound counter-clockwise from outside.
  std::vector<Eigen::Vector3f> positions;
  std::vector<Eigen::Vector3f> normals;
  z13::Rgba color {};
  // Bumped by the owner whenever the triangles change; raylib_module re-uploads on a new value.
  uint64_t revision {};
};

}  // namespace z13::render
