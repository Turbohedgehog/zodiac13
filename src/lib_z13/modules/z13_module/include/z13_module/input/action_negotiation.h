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

#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <span>
#include <string>
#include <vector>

#include <flecs.h>

#include <z13/components/input.h>

namespace z13::gameplay::input {

constexpr size_t kMaxNegotiatedActions = static_cast<size_t>(std::numeric_limits<uint16_t>::max()) + 1;
constexpr size_t kMaxActionNameLength = 128;
constexpr size_t kMaxActionsPerClient = 1024;
constexpr size_t kMaxRemoteActions = 4096;

struct ActionDescriptor {
  std::string enum_name;
  std::string value_name;
  z13::input::ActionInfo::EnumValueType enum_value {};
};

std::vector<ActionDescriptor> DescribeActions(const z13::input::ActionMap& action_map);

// Unknown actions get the next free id, never reused; nothing is registered if any is refused.
std::expected<std::vector<uint32_t>, std::string> RegisterRemoteActions(
    z13::input::ActionMap& action_map, std::span<const ActionDescriptor> actions);

// `ids` follow the DescribeActions order.
std::expected<void, std::string> AdoptActionIds(flecs::world world, std::span<const uint32_t> ids);

void NotifyConfigUpdated(flecs::world world);

}  // namespace z13::gameplay::input
