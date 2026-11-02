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

namespace z13::gravity_module {

// The module's name, which names its palette fragment (assets/station/palette/gravity.json).
inline constexpr std::string_view kModuleName = "gravity";

// A generator block carries a port of this network and role; it joins no conduit.
inline constexpr std::string_view kNetworkKey = "gravity";
inline constexpr std::string_view kGeneratorRole = "generator";

// Raises the field of each powered gravity generator of the main grid over `physics.generator_ramp_ticks` ticks, drops
// it when the power goes, and publishes the field of all of them as GravitySources for station_module's rooms.
class GeneratorSystem {
 public:
  static void Register(flecs::world& world);
};

}  // namespace z13::gravity_module
