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

#include "atmos_overlay_system.h"

#include <algorithm>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <flecs.h>

#include <lib_core/world/lifecycle.h>

#include <atmos/air.h>
#include <rooms/room_cache.h>
#include <z13_settings/atmos_tuning.h>
#include <z13/components/utility/atmosphere.h>
#include <z13/components/station/grid.h>
#include <z13/components/station/overlay_layers.h>
#include <z13/components/station/station_mode.h>
#include <z13/components/utility/utilities.h>

#include "room_air_carry.h"
#include "room_air_store.h"

namespace z13::atmos_module {

namespace {

using z13::station::OverlayBox;
using z13::station::OverlayLayers;
using z13::utility::RoomAirEntry;
namespace atmos = z13::utility::atmos;

constexpr std::string_view kLayerName = "pressure";

constexpr int64_t kPascalsPerKilopascal = 1000;
constexpr int64_t kPercentDivisor = 100;

constexpr z13::Rgba kEmptyColor {220, 70, 60, 255};
constexpr z13::Rgba kThinColor {240, 160, 60, 255};
constexpr z13::Rgba kNormalColor {80, 220, 100, 255};
constexpr z13::Rgba kThickColor {80, 160, 255, 255};

int64_t PascalsOfPercent(int32_t percent) {
  return atmos::kStandardPascals * percent / kPercentDivisor;
}

z13::Rgba ColorOf(int64_t pascals, const z13::AtmosTuning& tuning) {
  if (pascals < PascalsOfPercent(tuning.overlay_empty_percent)) {
    return kEmptyColor;
  }
  if (pascals < PascalsOfPercent(tuning.overlay_thin_percent)) {
    return kThinColor;
  }
  return pascals > PascalsOfPercent(tuning.overlay_thick_percent) ? kThickColor : kNormalColor;
}

void Fill(z13::station::OverlayLayer& layer, const std::vector<RoomAirEntry>& air,
          const z13::station::rooms::RoomGraph& graph, const z13::AtmosTuning& tuning) {
  layer.boxes.clear();
  const std::vector<z13::station::rooms::RoomId> ids = graph.ByAnchor();
  int64_t lowest = 0;
  int64_t highest = 0;
  for (size_t i = 0; i < ids.size() && i < air.size(); ++i) {
    const auto& room = graph.rooms.at(ids[i]);
    const int64_t pascals = atmos::PressurePascals(AirOf(air[i]), room.volume_cells);
    lowest = i == 0 ? pascals : std::min(lowest, pascals);
    highest = std::max(highest, pascals);
    layer.boxes.push_back({.min = room.bounds.min, .extent = room.bounds.extent, .color = ColorOf(pascals, tuning)});
  }
  layer.label = std::format("Pressure: {} rooms, {} to {} kPa", ids.size(), lowest / kPascalsPerKilopascal,
                            highest / kPascalsPerKilopascal);
}

void RegisterSystems(flecs::world world) {
  world.get_mut<OverlayLayers>().layers.push_back({.name = std::string(kLayerName)});
  const RoomAirQuery stored = world.query<const RoomAirEntry>();
  world.system<OverlayLayers, const z13::AtmosTuning>("AtmosOverlaySystem::Update")
      .kind<z13::utility::UtilityConsumersPhase>()
      .with<z13::station::StationMode>()
      .read<RoomAirEntry>()
      .read<z13::station::rooms::RoomCache>()
      .each([stored](flecs::iter& it, size_t, OverlayLayers& layers, const z13::AtmosTuning& tuning) {
        z13::station::OverlayLayer* layer = layers.Find(std::string(kLayerName));
        if (layer == nullptr) {
          return;
        }
        if (!layers.IsShown(layer->name)) {
          *layer = {.name = layer->name};
          return;
        }
        const auto cache = z13::station::MainGridData<z13::station::rooms::RoomCache>(it.world());
        const auto graph = cache ? cache->get().Current() : std::nullopt;
        if (graph) {
          Fill(*layer, LoadRoomAir(stored), graph->get(), tuning);
        }
      });
}

}  // namespace

void AtmosOverlaySystem::Register(flecs::world& world) {
  z13::OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::atmos_module
