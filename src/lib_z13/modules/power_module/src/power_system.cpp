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


#include "power_system.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <numeric>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <flecs.h>

#include <lib_core/state/world_serializer.h>
#include <lib_core/state/world_state.h>
#include <lib_core/time/simulation_clock.h>
#include <lib_core/utils/log.h>
#include <lib_core/world/lifecycle.h>

#include <networks/station_networks.h>
#include <power/balance.h>
#include <power/port_names.h>
#include <primitives/palette.h>
#include <z13/components/block.h>
#include <z13/components/power.h>
#include <z13/components/station_mode.h>
#include <z13/components/utilities.h>
#include <z13_settings/power_tuning.h>

namespace z13::power_module {

namespace {

using z13::building::primitives::BlockPalette;
using z13::building::primitives::PortSpec;
using z13::station::Block;
using z13::utility::BatteryCharge;
using z13::utility::UtilityDemand;
using z13::utility::UtilitySupply;
using z13::utility::networks::Network;
using z13::utility::networks::StationNetworks;
namespace power = z13::utility::power;

// When the networks were last balanced, to balance again between steps when they or the world changed.
struct Balanced {
  bool seen {};
  uint64_t generation {};
  uint64_t restores {};
};

// One network's parts and the entities of its consumers and batteries, in the Grid's order.
struct NetworkParts {
  power::Grid grid;
  std::vector<flecs::entity> consumers;
  std::vector<flecs::entity> batteries;
};

// The ports of a network arrive in cell order, so ties between equal priorities break the same way on every peer.
NetworkParts PartsOf(const flecs::world& world, const Network& network, const BlockPalette& palette) {
  NetworkParts parts;
  for (const uint64_t id : network.ports) {
    const flecs::entity block = world.entity(id);
    const auto primitive = palette.palette.Find(block.get<Block>().spec.type_id);
    const auto port = primitive ? power::PowerPortOf(*primitive) : std::nullopt;
    if (!port) {
      continue;
    }
    const PortSpec& spec = *port;
    if (spec.role == power::kProducerRole) {
      parts.grid.producers.push_back({.id = id, .watts = spec.Value(power::kOutputValue).value_or(0)});
    } else if (spec.role == power::kConsumerRole) {
      parts.grid.consumers.push_back({.id = id,
                                      .watts = spec.Value(power::kDemandValue).value_or(0),
                                      .priority = power::PriorityOf(spec.priority).value_or(power::kLowestPriority)});
      parts.consumers.push_back(block);
    } else if (spec.role == power::kStorageRole) {
      const auto* charge = block.try_get<BatteryCharge>();
      parts.grid.batteries.push_back({.id = id,
                                      .capacity = spec.Value(power::kCapacityValue).value_or(0),
                                      .charge = charge != nullptr ? charge->watt_ticks : 0,
                                      .rate = spec.Value(power::kRateValue).value_or(0)});
      parts.batteries.push_back(block);
    }
  }
  return parts;
}

z13::utility::PowerNetworkStatus StatusOf(const power::Balance& balance) {
  return {.produced = balance.produced,
          .demanded = balance.demanded,
          .served = balance.served,
          .stored = std::accumulate(balance.charges.begin(), balance.charges.end(), int64_t {})};
}

void Apply(const NetworkParts& parts, const power::Balance& balance) {
  for (size_t i = 0; i < parts.consumers.size(); ++i) {
    const flecs::entity consumer = parts.consumers[i];
    const power::Consumer& asked = parts.grid.consumers[i];
    const UtilityDemand demand {
        .network = std::string(power::kNetworkKey),
        .amount = static_cast<int>(asked.watts),
        .priority = asked.priority};
    const UtilitySupply supply {.satisfied = balance.supplies[i].satisfied};
    if (const auto* held = consumer.try_get<UtilityDemand>(); held == nullptr || !(*held == demand)) {
      consumer.set(demand);
    }
    if (const auto* held = consumer.try_get<UtilitySupply>(); held == nullptr || !(*held == supply)) {
      consumer.set(supply);
    }
  }
  for (size_t i = 0; i < parts.batteries.size(); ++i) {
    const flecs::entity battery = parts.batteries[i];
    const BatteryCharge* held = battery.try_get<BatteryCharge>();
    if (balance.charges[i] != (held != nullptr ? held->watt_ticks : 0)) {
      battery.set(BatteryCharge {.watt_ticks = balance.charges[i]});
    }
  }
}

void Update(flecs::iter& it, Balanced& balanced, z13::utility::PowerStatus& status, const StationNetworks& stations,
            const BlockPalette& palette,
            const z13::PowerTuning& tuning, const z13::flecs_tools::SimulationClock& clock,
            const z13::flecs_tools::WorldRestores& restores) {
  const bool step = clock.tick % tuning.step_ticks == 0;
  const bool changed =
      !balanced.seen || balanced.generation != stations.generation || balanced.restores != restores.count;
  if (!step && !changed) {
    return;
  }
  balanced = {.seen = true, .generation = stations.generation, .restores = restores.count};
  const flecs::world world = it.world();
  status.networks.clear();
  for (size_t index = 0; index < stations.networks.All().size(); ++index) {
    const Network& network = stations.networks.All()[index];
    if (network.key != power::kNetworkKey) {
      continue;
    }
    const NetworkParts parts = PartsOf(world, network, palette);
    const power::Balance balance = power::Solve(parts.grid, step ? tuning.step_ticks : 0);
    Apply(parts, balance);
    status.networks.push_back({.network_index = index, .status = StatusOf(balance)});
  }
}

void RegisterComponents(flecs::world world) {
  z13::flecs_tools::RegisterComponents<BatteryCharge, z13::utility::PowerStatus>(world);
}

void CheckPalette(const BlockPalette& palette) {
  for (const auto& primitive : palette.palette.primitives) {
    for (const auto& port : primitive.ports) {
      if (port.network != power::kNetworkKey) {
        continue;
      }
      if (const auto problem = power::ValidatePort(port)) {
        log_error("PowerSystem: primitive {} '{}': {}", primitive.id, primitive.name, *problem);
      }
    }
  }
}

void RegisterSystems(flecs::world world) {
  world.set(z13::utility::PowerStatus {});
  if (const auto* palette = world.try_get<BlockPalette>()) {
    CheckPalette(*palette);
  }
  world.entity("PowerNetwork").set(z13::utility::UtilityProvider {.network = std::string(power::kNetworkKey)});

  const auto balanced = std::make_shared<Balanced>();
  world.system<z13::utility::PowerStatus, const StationNetworks, const BlockPalette, const z13::PowerTuning,
               const z13::flecs_tools::SimulationClock, const z13::flecs_tools::WorldRestores>("PowerSystem::Balance")
      .kind<z13::utility::UtilitySimulationPhase>()
      .with<z13::station::StationMode>()
      .read<Block>()
      .write<UtilityDemand>()
      .write<UtilitySupply>()
      .write<BatteryCharge>()
      .each([balanced](flecs::iter& it, size_t, z13::utility::PowerStatus& status, const StationNetworks& stations,
                       const BlockPalette& palette,
                       const z13::PowerTuning& tuning, const z13::flecs_tools::SimulationClock& clock,
                       const z13::flecs_tools::WorldRestores& restores) {
        Update(it, *balanced, status, stations, palette, tuning, clock, restores);
      });
}

}  // namespace

void PowerSystem::Register(flecs::world& world) {
  z13::OnRegisterComponents(world, RegisterComponents);
  z13::OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::power_module
