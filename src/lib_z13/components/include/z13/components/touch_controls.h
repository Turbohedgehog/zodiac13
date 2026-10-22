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
#include <optional>
#include <vector>

#include <Eigen/Core>
#include <Eigen/Geometry>

namespace z13::input {

// What a finger works, chosen where it first touched (docs/android-plan.md, "Мультитач").
enum class TouchControl {
  kStick, kLook, kJump, kCrouch, kUse, kFlashlight, kToolMode, kPrimary, kStats, kBrush,
  kPreviousPrimitive, kNextPrimitive, kTurnZ, kTurnY, kTurnX,
  kThicker, kThinner, kMergeAt, kFlipDiagonal, kBend, kDestroy,
  kCell1, kCell2, kCell3, kCell4, kCell5, kCell6, kCell7, kCell8, kCell9,
};

// Which of the tool's buttons show and answer (touch/touch_mapping.h): the build tool's mode picks it.
enum class TouchLayer { kWalk, kBlock, kWall, kMerge, kBridge };

struct TouchFinger {
  uint64_t finger {};
  TouchControl control {};
  // Where it first touched and where it is, in pixels.
  Eigen::Vector2f origin = Eigen::Vector2f::Zero();
  Eigen::Vector2f position = Eigen::Vector2f::Zero();
};

// The fingers on the screen (client only, never state); the HUD draws it.
struct TouchControls {
  using Singleton = void;
  std::vector<TouchFinger> fingers;
  Eigen::Vector2f screen_size = Eigen::Vector2f::Zero();
  // Set by the first touch: a screen nobody touched shows no touch controls.
  bool used {};
  TouchLayer layer {};
  // The stats readout as the GUI last drew it, in fractions of the screen; a tap on it opens
  // the full stats panel, and a tap on the panel folds it back (F3 on PC).
  std::optional<Eigen::AlignedBox2f> stats_area;
  bool stats_shown {};
  // The brush row's cells as last drawn, by palette slot, in fractions of the screen; empty
  // while no build tool is in hand. A tap picks that primitive, like the palette window.
  std::vector<Eigen::AlignedBox2f> brush_areas;
};

}  // namespace z13::input
