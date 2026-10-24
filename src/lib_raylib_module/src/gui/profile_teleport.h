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
#include <flecs.h>

#include <lib_core/utils/status.h>

namespace z13::raylib::gui {

// A measuring aid, not a game action: puts the local player at `position_m` (x, y) on the floor
// they stand on and stops their fall. Refused when anyone else could see it, i.e. with another player
// in the game or a server connection.
z13::Status TeleportLocalPlayer(flecs::world world, const Eigen::Vector2f& position_m);

}  // namespace z13::raylib::gui
