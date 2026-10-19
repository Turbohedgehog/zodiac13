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

#include <string_view>

#include <z13/components/building.h>
#include <z13/components/touch_controls.h>

namespace z13::building {

// What a build tool mode is to the rest of the game, in one place: a new mode adds a case here.
struct ToolModeInfo {
  std::string_view label;
  z13::input::TouchLayer touch_layer {};
  // The wheel steps the mode's setting (wall thickness, merge target) instead of the hotbar.
  bool uses_wheel {};
};

constexpr ToolModeInfo ToolModeInfoOf(ToolMode mode) {
  using z13::input::TouchLayer;
  switch (mode) {
    case ToolMode::kBuild:
      return {.label = "Build", .touch_layer = TouchLayer::kBlock};
    case ToolMode::kDestroy:
      return {.label = "Destroy", .touch_layer = TouchLayer::kBlock};
    case ToolMode::kCut:
      return {.label = "Cut", .touch_layer = TouchLayer::kBlock};
    case ToolMode::kCutIn:
      return {.label = "Cut in", .touch_layer = TouchLayer::kBlock};
    case ToolMode::kWall:
      return {.label = "Wall", .touch_layer = TouchLayer::kWall, .uses_wheel = true};
    case ToolMode::kMerge:
      return {.label = "Merge", .touch_layer = TouchLayer::kMerge, .uses_wheel = true};
  }
  return {};
}

}  // namespace z13::building
