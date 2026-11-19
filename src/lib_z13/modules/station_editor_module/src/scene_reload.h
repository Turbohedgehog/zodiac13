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

#include <optional>

#include <Eigen/Dense>

namespace z13::station_editor {

// A reload of the station scene under way: `editor_reload_scene` removes Gameplay and asks for this, a frame later
// Gameplay is added again, and the frame after that the local player is put back at `pose`. Runtime only: not state.
struct SceneReload {
  using Singleton = void;

  bool pending {};
  std::optional<Eigen::Matrix4f> pose;
};

}  // namespace z13::station_editor
