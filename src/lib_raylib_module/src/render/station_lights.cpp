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

#include "station_lights.h"

#include <algorithm>
#include <utility>

#include <primitives/lights.h>
#include <rooms/light_reach.h>

#include "../tools/math_convert.h"
#include "view_culling.h"

namespace z13::raylib {

namespace {

LightsSyncKey KeyOf(StationLights::OptionalPalette palette, std::optional<uint64_t> rooms_fingerprint) {
  return {
      .palette_hash = palette ? std::optional(palette->get().hash) : std::nullopt,
      .rooms_fingerprint = rooms_fingerprint,
  };
}

Eigen::AlignedBox3f InMeters(const Eigen::AlignedBox3f& cells) {
  return {cells.min() * z13::station::kCellSize, cells.max() * z13::station::kCellSize};
}

FrameLight InMeters(const z13::building::primitives::PointLight& light, const z13::station::rooms::LightReach& reach) {
  const float cell = z13::station::kCellSize;
  return {
      .position = light.position * cell,
      .radius = static_cast<float>(light.source.radius_cells) * cell,
      .color = ToUnitColor(light.source.color).head<3>() * light.source.intensity,
      .reach = InMeters(reach.reach),
      .radius_box = InMeters(reach.radius_box),
  };
}

}  // namespace

bool StationLights::SyncedWith(OptionalPalette palette, std::optional<uint64_t> rooms_fingerprint) const {
  return synced_with_ == KeyOf(palette, rooms_fingerprint);
}

void StationLights::Sync(std::span<const z13::station::Block> blocks, OptionalPalette palette,
                         z13::station::rooms::OptionalRoomGraph rooms, std::optional<uint64_t> rooms_fingerprint) {
  lights_ = {};
  by_room_ = {};
  for (const z13::building::primitives::PointLight& light : z13::building::primitives::LightsOf(blocks, palette)) {
    const auto reach = z13::station::rooms::ReachOf(light, rooms);
    if (!reach) {
      continue;
    }
    for (const z13::station::rooms::RoomIndex room : reach->lit_rooms) {
      by_room_[room].push_back(lights_.size());
    }
    lights_.push_back({.light = InMeters(light, *reach), .room = reach->room});
  }
  synced_with_ = KeyOf(palette, rooms_fingerprint);
}

// The others are tested nearest first: the culling test is the costly part.
std::vector<FrameLight> StationLights::Visible(
    const ViewCulling& culling, const Eigen::Vector3f& eye, size_t count) const {
  const auto seen_rooms = culling.SeenRooms();
  std::vector<bool> seen(lights_.size());
  if (seen_rooms) {
    for (const z13::station::rooms::RoomIndex room : *seen_rooms) {
      if (const auto lights = by_room_.find(room); lights != by_room_.end()) {
        std::ranges::for_each(lights->second, [&seen](size_t index) { seen[index] = true; });
      }
    }
  }
  std::vector<std::pair<float, size_t>> by_distance;
  for (size_t i = 0; i < lights_.size(); ++i) {
    // Rooms cull: a lamp lighting no seen room lights nothing in view.
    if (!seen_rooms || seen[i] || !lights_[i].room) {
      by_distance.emplace_back((lights_[i].light.position - eye).squaredNorm(), i);
    }
  }
  std::ranges::sort(by_distance);
  std::vector<FrameLight> visible;
  for (const auto& [distance, index] : by_distance) {
    if (visible.size() == count) {
      break;
    }
    if (seen[index] || culling.Visible(lights_[index].light.reach)) {
      visible.push_back(lights_[index].light);
    }
  }
  return visible;
}

}  // namespace z13::raylib
