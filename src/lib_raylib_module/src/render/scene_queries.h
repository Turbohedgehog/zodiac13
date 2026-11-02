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

#include <Eigen/Dense>

#include <z13/components/gameplay/ghost.h>
#include <z13/components/gameplay/player.h>
#include <z13/components/gameplay/player_parts.h>

namespace z13::raylib {

using RemotePlayerQuery = flecs::query<const gameplay::Player, const Eigen::Matrix4f>;
using GhostQuery = flecs::query<const gameplay::GhostPlayer, const Eigen::Matrix4f>;
using DrawnQuery = flecs::query<const Eigen::Matrix4f>;
using FlashlightQuery = flecs::query<const gameplay::Flashlight, const Eigen::Matrix4f>;

}  // namespace z13::raylib
