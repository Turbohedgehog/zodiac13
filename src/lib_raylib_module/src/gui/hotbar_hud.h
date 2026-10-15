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
#include <string_view>
#include <vector>

#include <Eigen/Geometry>

#include <flecs.h>

#include <z13_settings/settings.h>

namespace z13::raylib::gui {

// The local player's hotbar where the touch cells are, and the build tool's mode under the
// crosshair while it is in hand.
void DrawHotbar(flecs::world world, const z13::TouchLayout& layout);

// While a build tool is in hand, every palette primitive by name above the hotbar, the brush's
// highlighted; returns their cells by palette slot, in ImGui units, for the touch layer.
std::vector<Eigen::AlignedBox2f> DrawBrushRow(flecs::world world, const z13::TouchLayout& layout);

// The mode the local player's build tool works in, while it is in hand.
std::optional<std::string_view> LocalToolAction(flecs::world world);

}  // namespace z13::raylib::gui
