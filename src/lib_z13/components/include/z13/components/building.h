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

namespace z13::building {

// Where the brush follows its player and build requests are handled; BlockBuildingSystem
// orders its own building after it.
struct UpdateBuildingToolPhase {};

// On a player while a build tool is in the active hotbar cell; derived every frame, never state.
struct BuildingTool {};

// The order is the one the mode key cycles through.
enum class ToolMode : uint8_t { kBuild, kDestroy, kCut, kCutIn, kWall };

// On a build tool item, with its BlockBrush: a tool given away builds the same.
struct BuildTool {
  using State = void;
  ToolMode mode {};
  // The thickness, in cells, of the walls the Wall mode lays.
  int wall_thickness = 1;
};

// The mode the tool works in this frame: a held cut key overrides the tool's own on PC.
inline ToolMode EffectiveToolMode(ToolMode mode, bool cut_held, bool cut_in_held) {
  if (cut_held) {
    return ToolMode::kCut;
  }
  return cut_in_held ? ToolMode::kCutIn : mode;
}

struct Brush {
  float distance {};
};

// Station mode: the build key went down, so a drag starts at the brush.
struct RequestBrushDrag {};
struct RequestBuildBlock {};

// The build modifiers held this frame, for as long as they are; derived from the input
// every frame, never state. A build with CutModifier cuts its box out of the station
// instead; with CutInModifier it cuts its place out first.
struct CutModifier {};
struct CutInModifier {};
struct RequestDestroyBlock {};

}  // namespace z13::building
