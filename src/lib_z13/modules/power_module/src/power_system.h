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

namespace z13::power_module {

// The module's name, which names its palette fragment (assets/station/palette/power.json).
inline constexpr std::string_view kModuleName = "power";

// Balances every power network: producers and batteries against consumers, once per `PowerTuning::step_ticks`
// and whenever the networks change, and writes each consumer's UtilitySupply.
class PowerSystem {
 public:
  static void Register(flecs::world& world);
};

}  // namespace z13::power_module
