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

#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include <Eigen/Dense>
#include <flecs.h>

#include <editor_camera/orbit_camera.h>
#include <station_graph/graph_editor.h>
#include <z13/components/editor/editor_camera.h>
#include <z13_settings/settings.h>

namespace z13::station_editor {

inline constexpr std::string_view kNoGraph = "no graph loaded: call editor_load first";

std::expected<std::reference_wrapper<z13::editor::graph::GraphEditor>, std::string> LoadedEditor(flecs::world world);

std::optional<Eigen::Matrix4f> LocalPlayerPose(flecs::world world);

// The ray of the orbit camera through a pixel of the window; none while the camera is off or has not drawn.
std::optional<z13::editor::camera::Ray> PixelRay(const z13::editor::EditorCamera& view,
                                                 const z13::EditorCameraTuning& tuning, const Eigen::Vector2f& pixel);

}  // namespace z13::station_editor
