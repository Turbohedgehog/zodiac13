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

#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include <atmos/devices.h>
#include <primitives/palette.h>

// What a gas port's strings and value names mean (palette fragment assets/station/palette/atmos.json).
namespace z13::utility::atmos {

inline constexpr std::string_view kNetworkKey = "gas";

// A unit holds the rooms of the vents on its network at the values it names; a vent is where it reaches a room.
inline constexpr std::string_view kUnitRole = "unit";
inline constexpr std::string_view kVentRole = "vent";
// An airlock controller stands in the chamber between the two doors it runs; it joins no pipe.
inline constexpr std::string_view kAirlockRole = "airlock";

// The unit's target pressure (pascals) and temperature (kelvin), and the most it does in a step: carbon dioxide
// scrubbed and air added (millimoles), heat moved (millijoules).
inline constexpr std::string_view kPascalsValue = "pascals";
inline constexpr std::string_view kKelvinValue = "kelvin";
inline constexpr std::string_view kScrubValue = "scrub";
inline constexpr std::string_view kFillValue = "fill";
inline constexpr std::string_view kHeatValue = "heat";

std::optional<std::reference_wrapper<const z13::building::primitives::PortSpec>> GasPortOf(
    const z13::building::primitives::Primitive& primitive);

std::optional<Regulation> RegulationOf(const z13::building::primitives::PortSpec& port);

std::optional<std::string> ValidatePort(const z13::building::primitives::PortSpec& port);

}  // namespace z13::utility::atmos
