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

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include <primitives/palette.h>

// What a power port's strings and value names mean (palette fragment assets/station/palette/power.json).
namespace z13::utility::power {

inline constexpr std::string_view kNetworkKey = "power";

inline constexpr std::string_view kProducerRole = "producer";
inline constexpr std::string_view kConsumerRole = "consumer";
inline constexpr std::string_view kStorageRole = "storage";
// Conducts only: a breaker.
inline constexpr std::string_view kConductorRole = "conductor";

// Watts a producer gives; watts a consumer asks for; what a storage holds (watt-ticks) and the watts it moves.
inline constexpr std::string_view kOutputValue = "output";
inline constexpr std::string_view kDemandValue = "demand";
inline constexpr std::string_view kCapacityValue = "capacity";
inline constexpr std::string_view kRateValue = "rate";

// Served first to last when the network falls short.
inline constexpr uint8_t kLowestPriority = 4;

// The priority named by a port's `priority` string; nothing for a name the network doesn't know.
std::optional<uint8_t> PriorityOf(std::string_view name);

// The primitive's port on the power network, if it has one.
std::optional<std::reference_wrapper<const z13::building::primitives::PortSpec>> PowerPortOf(
    const z13::building::primitives::Primitive& primitive);

// Why a power port of the palette can't work (an unknown role or priority, a value it needs missing), per primitive.
std::optional<std::string> ValidatePort(const z13::building::primitives::PortSpec& port);

}  // namespace z13::utility::power
