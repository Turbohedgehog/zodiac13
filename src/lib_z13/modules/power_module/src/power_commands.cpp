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



#include "power_commands.h"

#include <cstdint>
#include <string>
#include <vector>

#include <lib_core/commands/command_registry.h>
#include <lib_core/state/world_state.h>
#include <networks/station_networks.h>
#include <power/port_names.h>
#include <primitives/palette.h>
#include <z13/components/station/block.h>
#include <z13/components/utility/power.h>
#include <z13/components/utility/utilities.h>

namespace z13::power_module {

namespace {

using z13::building::primitives::BlockPalette;
using z13::utility::PowerStatus;
using z13::utility::networks::StationNetworks;
namespace power = z13::utility::power;

struct CellInfo {
  int x {};
  int y {};
  int z {};
};

struct PowerBlockInfo {
  std::string name;
  std::string primitive;
  CellInfo cell;
  std::string role;       // producer, consumer, storage or conductor
  bool supplied {};       // a consumer's demand is met (true for the others)
  int64_t charge_watt_ticks {};  // a battery's energy
};

struct PowerNetworkInfo {
  int conduit_runs {};
  int64_t produced_w {};
  int64_t demanded_w {};
  int64_t served_w {};
  int64_t stored_watt_ticks {};
  std::vector<PowerBlockInfo> blocks;
};

struct PowerNetworksCommand {
  using Command = z13::Command<
      std::vector<PowerNetworkInfo>,
      "Lists the station's power networks (cables and the blocks on them) as of the last balance: watts produced, "
      "asked for and served, the energy stored in batteries (watt-ticks, 1 W for one tick) and each block with its "
      "role and whether it is supplied. A network without a producer or charged battery serves nothing.",
      Tags<kDebugTag>>;
  int unused {};
};

void ListNetworks(flecs::world world, ReplyOf<PowerNetworksCommand>& reply) {
  const auto& stations = world.get<StationNetworks>();
  const auto& status = world.get<PowerStatus>();
  const auto& palette = world.get<BlockPalette>().palette;
  std::vector<PowerNetworkInfo> listed;
  for (const auto& entry : status.networks) {
    const auto& network = stations.networks.All()[entry.network_index];
    PowerNetworkInfo info {.conduit_runs = static_cast<int>(network.runs.size()),
                           .produced_w = entry.status.produced,
                           .demanded_w = entry.status.demanded,
                           .served_w = entry.status.served,
                           .stored_watt_ticks = entry.status.stored};
    for (const uint64_t id : network.ports) {
      const flecs::entity block = world.entity(id);
      const auto& placed = block.get<z13::station::Block>();
      const auto primitive = palette.Find(placed.spec.type_id);
      const auto port = primitive ? power::PowerPortOf(*primitive) : std::nullopt;
      if (!port) {
        continue;
      }
      const auto* charge = block.try_get<z13::utility::BatteryCharge>();
      info.blocks.push_back({.name = std::string(block.name().c_str()),
                             .primitive = primitive->get().name,
                             .cell = {placed.cell.x(), placed.cell.y(), placed.cell.z()},
                             .role = port->get().role,
                             .supplied = z13::utility::IsSupplied(block),
                             .charge_watt_ticks = charge ? charge->watt_ticks : 0});
    }
    listed.push_back(std::move(info));
  }
  reply.Succeed(std::move(listed));
}

}  // namespace

void RegisterPowerCommandComponents(flecs::world world) {
  z13::flecs_tools::RegisterComponents<PowerNetworksCommand>(world);
}

void RegisterPowerCommandSystems(flecs::world world) {
  CommandSystem<PowerNetworksCommand>(
      world, "PowerCommands::List",
      [](flecs::iter& it, size_t, const PowerNetworksCommand&, ReplyOf<PowerNetworksCommand>& reply) {
        ListNetworks(it.world(), reply);
      });
}

}  // namespace z13::power_module
