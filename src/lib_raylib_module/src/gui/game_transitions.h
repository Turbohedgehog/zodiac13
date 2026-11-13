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

#include "game_scenes.h"

namespace z13::raylib::gui {

// Starts a local game in `scene` (after a frame with the loading screen), as the Start Game window does.
void StartGame(flecs::world world, const GameScene& scene);

// Leaves the game for the main menu (Gameplay goes, Pause stays), as the pause menu does.
void LeaveGame(flecs::world world);

}  // namespace z13::raylib::gui
