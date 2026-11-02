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

#include <atmos/port_names.h>

#include <algorithm>
#include <array>
#include <format>

namespace z13::utility::atmos {

namespace {

constexpr std::array kUnitValues = {kPascalsValue, kKelvinValue, kScrubValue, kFillValue, kHeatValue};

}  // namespace

std::optional<std::reference_wrapper<const z13::building::primitives::PortSpec>> GasPortOf(
    const z13::building::primitives::Primitive& primitive) {
  const auto found = std::ranges::find(primitive.ports, kNetworkKey, &z13::building::primitives::PortSpec::network);
  if (found == primitive.ports.end()) {
    return std::nullopt;
  }
  return std::cref(*found);
}

std::optional<Regulation> RegulationOf(const z13::building::primitives::PortSpec& port) {
  const auto pascals = port.Value(kPascalsValue);
  const auto kelvin = port.Value(kKelvinValue);
  const auto scrub = port.Value(kScrubValue);
  const auto fill = port.Value(kFillValue);
  const auto heat = port.Value(kHeatValue);
  if (port.role != kUnitRole || !pascals || !kelvin || !scrub || !fill || !heat) {
    return std::nullopt;
  }
  return Regulation {.pascals = *pascals,
                     .kelvin = *kelvin,
                     .scrub_millimoles = *scrub,
                     .fill_millimoles = *fill,
                     .heat_millijoules = *heat};
}

std::optional<std::string> ValidatePort(const z13::building::primitives::PortSpec& port) {
  if (port.role == kVentRole) {
    return std::nullopt;
  }
  if (port.role != kUnitRole) {
    return std::format("unknown role '{}'", port.role);
  }
  const auto missing = std::ranges::find_if(kUnitValues, [&port](std::string_view name) { return !port.Value(name); });
  if (missing != kUnitValues.end()) {
    return std::format("a {} port needs a '{}' value", port.role, *missing);
  }
  return std::nullopt;
}

}  // namespace z13::utility::atmos
