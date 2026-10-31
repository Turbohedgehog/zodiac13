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


#include <power/port_names.h>

#include <algorithm>
#include <array>
#include <format>
#include <string>

namespace z13::utility::power {

namespace {

struct NamedPriority {
  std::string_view name;
  uint8_t priority {};
};

constexpr std::array kPriorities = {NamedPriority {"life_support", 0}, NamedPriority {"doors", 1},
                                    NamedPriority {"gravity", 2},      NamedPriority {"lights", 3},
                                    NamedPriority {"other", kLowestPriority}};

}  // namespace

std::optional<uint8_t> PriorityOf(std::string_view name) {
  const auto found = std::ranges::find(kPriorities, name, &NamedPriority::name);
  return found != kPriorities.end() ? std::optional(found->priority) : std::nullopt;
}

std::optional<std::reference_wrapper<const z13::building::primitives::PortSpec>> PowerPortOf(
    const z13::building::primitives::Primitive& primitive) {
  const auto found = std::ranges::find(primitive.ports, kNetworkKey, &z13::building::primitives::PortSpec::network);
  if (found == primitive.ports.end()) {
    return std::nullopt;
  }
  return std::cref(*found);
}

std::optional<std::string> ValidatePort(const z13::building::primitives::PortSpec& port) {
  const auto needs = [&port](std::string_view value) -> std::optional<std::string> {
    if (port.Value(value)) {
      return std::nullopt;
    }
    return std::format("a {} port needs a '{}' value", port.role, value);
  };
  if (port.role == kProducerRole) {
    return needs(kOutputValue);
  }
  if (port.role == kConsumerRole) {
    if (!PriorityOf(port.priority)) {
      return std::format("unknown priority '{}'", port.priority);
    }
    return needs(kDemandValue);
  }
  if (port.role == kStorageRole) {
    return needs(kCapacityValue).or_else([&] { return needs(kRateValue); });
  }
  if (port.role == kConductorRole) {
    return std::nullopt;
  }
  return std::format("unknown role '{}'", port.role);
}

}  // namespace z13::utility::power
