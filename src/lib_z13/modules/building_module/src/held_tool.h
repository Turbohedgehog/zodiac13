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

#include <optional>

#include <flecs.h>

namespace z13::building {

// The build tool in the player's active hotbar cell (ActiveItem), if the player holds one.
std::optional<flecs::entity> HeldBuildTool(flecs::entity player);

// The player's build tool in the lowest cell, in hand or not.
std::optional<flecs::entity> CarriedBuildTool(flecs::entity player);

}  // namespace z13::building
