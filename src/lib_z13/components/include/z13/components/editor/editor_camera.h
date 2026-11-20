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

namespace z13::editor {

// The camera looks at `target` from `distance` away. Cells, degrees; z is up, yaw 0 puts the eye on the +x side.
struct OrbitCamera {
  Eigen::Vector3f target = Eigen::Vector3f::Zero();
  float distance {};
  float yaw_degrees {};
  float pitch_degrees {};
};

struct EditorFinger {
  std::uint64_t finger {};
  Eigen::Vector2f position = Eigen::Vector2f::Zero();
};

// The editor's view of the station (docs/station-editor-plan.md): while `active` the renderer shows `orbit` instead of
// the player's eye, the cursor is free and the pointer works the camera. Client only, never state.
struct EditorCamera {
  using Singleton = void;

  bool active {};
  OrbitCamera orbit;
  bool pan_held {};
  bool orbit_held {};
  std::vector<EditorFinger> fingers;
};

}  // namespace z13::editor
