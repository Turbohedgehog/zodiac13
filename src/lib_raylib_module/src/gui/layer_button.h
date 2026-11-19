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

#include <Eigen/Dense>

#include <z13/components/station/overlay_layers.h>

namespace z13::raylib::gui {

// The touch screen's stand-in for F6, under the console's button; returns its area in ImGui units. A tap is the touch
// input's (TouchControls::layer_area).
Eigen::AlignedBox2f DrawLayerButton(const z13::station::OverlayLayers& layers);

}  // namespace z13::raylib::gui
