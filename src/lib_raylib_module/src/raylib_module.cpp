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

#include "raylib_module.h"

#include <flecs.h>

#include "gui/gui_system.h"
#include "raylib_system.h"
#include "render/environment_render_system.h"
#include "tools/input_publisher.h"

namespace z13::raylib {

RaylibRender::RaylibRender(flecs::world& world) {
  // RaylibSystem first: its InitWorldData observer brings up the SDL window / GL
  // context / rlgl, which the other systems need before they touch the GPU.
  RaylibSystem::Register(world);
  EnvironmentRenderSystem::Register(world);
  InputPublisher::Register(world);
  GuiSystem::Register(world);
}

}  // namespace z13::raylib
