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

namespace z13::raylib {

// Marks that the SDL window / GL context is up and rlgl is initialised. Singleton.
struct RaylibData {
  bool initialized = false;
};

// Current drawable size of the SDL window. Singleton, refreshed each frame.
struct WindowSize {
  int width = 0;
  int height = 0;
};

// Added when the OS window requested close; drives Core shutdown.
struct RaylibWindowClosed {};

// Frame phases, mirroring the Ogre module.
struct ReadEvents {};
struct PreRender {};
struct Render {};
struct PostRender {};
struct FinalizeRender {};

// Raw raylib input for the current frame. Singleton, refreshed in ReadEvents.
// Table-driven flecs events cover the bound keys; this is the escape hatch for
// systems that need more (wheel, absolute position, arbitrary keys via IsKeyDown).
struct RaylibInputFrame {
  float mouse_x = 0.f;
  float mouse_y = 0.f;
  float mouse_dx = 0.f;
  float mouse_dy = 0.f;
  float mouse_wheel = 0.f;
};

}  // namespace z13::raylib
