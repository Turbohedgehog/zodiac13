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

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <flecs.h>

// What a utility network (power, gas, ...) and the things that use it agree on (docs/station-systems-plan.md,
// "Модули и коммуникации"): a network module writes UtilitySupply, doors, lights and the rest only read it.
namespace z13::utility {

// Where the networks are rebuilt from the conduit runs and ports (conduit_module), after the rooms are built.
struct UtilityNetworksPhase {};
// Where a network module works out each consumer's supply, from those networks.
struct UtilitySimulationPhase {};
// Where consumers read it.
struct UtilityConsumersPhase {};

// What a consumer asks of a network. One network per entity for now; derived from the block's port, never state.
struct UtilityDemand {
  std::string network;
  int amount {};
  // Lower is served first when the network falls short.
  uint8_t priority {};

  bool operator==(const UtilityDemand&) const = default;
};

// What the network module gave a consumer; derived, never state. A consumer without one has no network module
// behind it, and counts as supplied.
struct UtilitySupply {
  bool satisfied {};

  bool operator==(const UtilitySupply&) const = default;
};

// On the entity of a module that runs a network.
struct UtilityProvider {
  std::string network;
};

// A module that runs a network, as the peers compare it in ClientHello.
struct UtilityModule {
  std::string name;
  // Grows with any change that makes the simulation differ.
  uint32_t version {};
  // Its primitives are in assets/station/palette/<name>.json.
  bool palette_fragment {};

  bool operator==(const UtilityModule&) const = default;
};

// The modules that run networks in this world. Each adds itself in the init-phases stage with DeclareUtilityModule,
// once building_module has registered the component and before it reads the list to load the palette.
struct UtilityModules {
  using Singleton = void;
  std::vector<UtilityModule> modules;

  // By name, so peers list them alike whatever order they loaded in.
  std::vector<UtilityModule> Sorted() const;
};

inline std::vector<UtilityModule> UtilityModules::Sorted() const {
  std::vector<UtilityModule> sorted = modules;
  std::ranges::sort(sorted, {}, &UtilityModule::name);
  return sorted;
}

inline void DeclareUtilityModule(flecs::world& world, UtilityModule module) {
  world.get_mut<UtilityModules>().modules.push_back(std::move(module));
}

inline bool IsSupplied(flecs::entity consumer) {
  const auto* supply = consumer.try_get<UtilitySupply>();
  return supply == nullptr || supply->satisfied;
}

}  // namespace z13::utility
