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

#include <string_view>

#include <flecs.h>

namespace z13::atmos_module {

// The module's name, which names its palette fragment (assets/station/palette/atmos.json).
inline constexpr std::string_view kModuleName = "atmos";

// Keeps the air of the main grid's rooms (RoomAirEntry): follows the rooms as they change and, every
// `AtmosTuning::step_ticks` ticks, lets it flow, breathes it, regulates it and sets DoorInterlock.
class AtmosSystem {
 public:
  static void Register(flecs::world& world);
};

}  // namespace z13::atmos_module
