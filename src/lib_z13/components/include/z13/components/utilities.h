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
#include <string>

#include <flecs.h>

// What a utility network (power, gas, ...) and the things that use it agree on (docs/station-systems-plan.md,
// "Модули и коммуникации"): a network module writes UtilitySupply, doors, lights and the rest only read it.
namespace z13::utility {

// Where a network module works out each consumer's supply, after the rooms are built.
struct UtilitySimulationPhase {};
// Where consumers read it.
struct UtilityConsumersPhase {};

// What a consumer asks of a network. One network per entity for now; derived from the block's port, never state.
struct UtilityDemand {
  std::string network;
  int amount {};
  // Lower is served first when the network falls short.
  uint8_t priority {};
};

// What the network module gave a consumer; derived, never state. A consumer without one has no network module
// behind it, and counts as supplied.
struct UtilitySupply {
  bool satisfied {};
};

// On the entity of a module that runs a network.
struct UtilityProvider {
  std::string network;
};

inline bool IsSupplied(flecs::entity consumer) {
  const auto* supply = consumer.try_get<UtilitySupply>();
  return supply == nullptr || supply->satisfied;
}

}  // namespace z13::utility
