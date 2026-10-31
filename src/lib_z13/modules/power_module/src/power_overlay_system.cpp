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


#include "power_overlay_system.h"

#include <format>
#include <optional>
#include <string>
#include <string_view>

#include <flecs.h>

#include <lib_core/world/lifecycle.h>

#include <networks/station_networks.h>
#include <power/port_names.h>
#include <primitives/palette.h>
#include <primitives/placement.h>
#include <z13/components/block.h>
#include <z13/components/conduit.h>
#include <z13/components/overlay_layers.h>
#include <z13/components/power.h>
#include <z13/components/station_mode.h>
#include <z13/components/utilities.h>

namespace z13::power_module {

namespace {

using z13::building::primitives::BlockPalette;
using z13::station::OverlayBox;
using z13::station::OverlayLayers;
using z13::utility::PowerStatus;
using z13::utility::networks::StationNetworks;
namespace power = z13::utility::power;

constexpr std::string_view kLayerName = "power";

constexpr z13::Rgba kLiveColor {80, 220, 100, 255};
constexpr z13::Rgba kDeadColor {220, 70, 60, 255};
constexpr z13::Rgba kProducerColor {240, 220, 70, 255};
constexpr z13::Rgba kStorageColor {80, 160, 255, 255};
constexpr z13::Rgba kConductorColor {230, 230, 230, 255};

// A network with something producing, or a battery with charge, is live; the rest are dead.
bool IsLive(const z13::utility::PowerNetworkStatus& status) {
  return status.produced > 0 || status.stored > 0;
}

z13::Rgba PortColor(const z13::building::primitives::PortSpec& port, flecs::entity block) {
  if (port.role == power::kProducerRole) {
    return kProducerColor;
  }
  if (port.role == power::kStorageRole) {
    return kStorageColor;
  }
  if (port.role == power::kConductorRole) {
    return kConductorColor;
  }
  const auto* supply = block.try_get<z13::utility::UtilitySupply>();
  return supply != nullptr && supply->satisfied ? kLiveColor : kDeadColor;
}

std::string LabelOf(const PowerStatus& status) {
  z13::utility::PowerNetworkStatus total;
  for (const auto& entry : status.networks) {
    total.produced += entry.status.produced;
    total.demanded += entry.status.demanded;
    total.served += entry.status.served;
    total.stored += entry.status.stored;
  }
  return std::format("Power: {} networks, {} W produced, {} of {} W served, {} watt-ticks stored", status.networks.size(),
                     total.produced, total.served, total.demanded, total.stored);
}

void Fill(z13::station::OverlayLayer& layer, const flecs::world& world, const StationNetworks& stations,
          const PowerStatus& status, const BlockPalette& palette) {
  layer.boxes.clear();
  layer.label = LabelOf(status);
  for (const auto& entry : status.networks) {
    const auto& network = stations.networks.All()[entry.network_index];
    const bool live = IsLive(entry.status);
    for (const uint64_t id : network.runs) {
      const z13::conduit::Conduit& run = world.entity(id).get<z13::conduit::Conduit>();
      Eigen::Vector3i extent = Eigen::Vector3i::Ones();
      extent[static_cast<int>(run.axis)] = run.length;
      layer.boxes.push_back({.min = run.cell, .extent = extent, .color = live ? kLiveColor : kDeadColor});
    }
    for (const uint64_t id : network.ports) {
      const flecs::entity block = world.entity(id);
      const auto& placed = block.get<z13::station::Block>();
      const auto primitive = palette.palette.Find(placed.spec.type_id);
      const auto port = primitive ? power::PowerPortOf(*primitive) : std::nullopt;
      if (!port) {
        continue;
      }
      const z13::building::primitives::CellBox cells = z13::building::primitives::OccupiedCells(placed);
      layer.boxes.push_back({.min = cells.min, .extent = cells.extent, .color = PortColor(*port, block)});
    }
  }
}

void RegisterSystems(flecs::world world) {
  world.get_mut<OverlayLayers>().layers.push_back({.name = std::string(kLayerName)});
  world.system<OverlayLayers, const StationNetworks, const PowerStatus, const BlockPalette>("PowerOverlaySystem::Update")
      .kind<z13::utility::UtilityConsumersPhase>()
      .with<z13::station::StationMode>()
      .read<z13::station::Block>()
      .read<z13::conduit::Conduit>()
      .each([](flecs::iter& it, size_t, OverlayLayers& layers, const StationNetworks& stations,
               const PowerStatus& status, const BlockPalette& palette) {
        z13::station::OverlayLayer* layer = layers.Find(std::string(kLayerName));
        if (layer == nullptr) {
          return;
        }
        if (!layers.IsShown(layer->name)) {
          *layer = {.name = layer->name};
          return;
        }
        Fill(*layer, it.world(), stations, status, palette);
      });
}

}  // namespace

void PowerOverlaySystem::Register(flecs::world& world) {
  z13::OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::power_module
