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

#include <lib_core/core_types.h>

namespace z13::raylib {

// Dear ImGui overlay (SDL3 + OpenGL3 backends) driving the pause-menu window
// stack shown while gameplay::Pause is set: main menu, settings, key bindings.
class GuiSystem {
 public:
  static void Register(flecs::world& world);

  // Tears down the ImGui context + backends. Must run while the GL context is
  // still alive, i.e. before SdlPlatform::Shutdown().
  static void ShutdownImGui();
};

}  // namespace z13::raylib
