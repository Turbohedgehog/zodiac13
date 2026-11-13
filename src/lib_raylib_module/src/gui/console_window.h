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

#include <flecs.h>

#include <z13/components/input/touch_controls.h>

#include "console_state.h"

struct SDL_Window;

namespace z13::raylib::gui {

// The console drawn over everything: a button to open it on a touch screen, and when it is down, the log with a
// command line, and the talk with the assistant, in two tabs. Call once a frame, inside the ImGui frame.
void DrawConsole(flecs::world world, ConsoleState& console, SDL_Window* window, bool touch_screen);

// Whether the console shows its button and soft keyboard: always on Android, elsewhere once a finger has touched
// the screen.
bool IsTouchScreen(const z13::input::TouchControls* touch);

void ToggleConsole(ConsoleState& console, bool touch_screen);

}  // namespace z13::raylib::gui
