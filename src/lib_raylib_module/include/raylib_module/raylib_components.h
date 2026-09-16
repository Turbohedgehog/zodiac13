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

namespace z13::raylib {

// Marks that the SDL window / GL context is up and rlgl is initialised. Singleton.
struct RaylibData {
  bool initialized {};
};

// Current drawable size of the SDL window. Singleton, refreshed each frame.
struct WindowSize {
  Eigen::Vector2i size = Eigen::Vector2i::Zero();
};

// Added when the OS window requested close; drives Core shutdown.
struct RaylibWindowClosed {};

struct ReadEvents {};
// Phase barrier after ReadEvents: same-phase order isn't guaranteed, so
// consumers of pumped events (e.g. GuiSystem::BeginFrame) run here instead.
struct ConsumeEvents {};
struct PreRender {};
struct Render {};
struct PostRender {};
struct FinalizeRender {};

// Raw raylib input for the current frame; escape hatch for what the table-driven
// flecs events don't cover (wheel, absolute position, arbitrary keys).
struct RaylibInputFrame {
  Eigen::Vector2f mouse_pos = Eigen::Vector2f::Zero();
  Eigen::Vector2f mouse_delta = Eigen::Vector2f::Zero();
  float mouse_wheel {};
};

}  // namespace z13::raylib
