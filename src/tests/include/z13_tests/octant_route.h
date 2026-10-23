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
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <Eigen/Dense>

#include <rooms/corridor_walk.h>
#include <rooms/room_graph.h>
#include <rooms/space_walk.h>
#include <rooms/tour_viewpoints.h>
#include <z13/components/block.h>
#include <z13_tests/virtual_render.h>

namespace z13::testing {

// Where the 19 fps were seen (release, 2026-10-22), in meters.
inline const Eigen::Vector3f kSlowSpotM {193.32f, 80.17f, 1.75f};

// How many rooms the corridor walk of OctantRoute may enter, and how many the tour picks.
inline constexpr size_t kRouteCorridorRooms = 40;
inline constexpr size_t kRouteTourRooms = 8;

// A viewpoint of a walk, in cells.
struct Stop {
  std::string label;
  Eigen::Vector3f eye = Eigen::Vector3f::Zero();
  Eigen::Vector3f target = Eigen::Vector3f::Zero();
};

inline void AddStops(
    std::vector<Stop>& stops, std::string_view prefix, const std::vector<z13::station::rooms::Viewpoint>& viewpoints) {
  for (size_t i = 0; i < viewpoints.size(); ++i) {
    stops.push_back(
        {.label = std::format("{}{}", prefix, i), .eye = viewpoints[i].eye, .target = viewpoints[i].target});
  }
}

// The walk the render benchmarks share: along the corridors to the slow spot, round the rooms and
// out round the station.
inline std::vector<Stop> OctantRoute(
    const z13::station::rooms::RoomGraph& graph, const std::vector<z13::station::Block>& blocks) {
  namespace rooms = z13::station::rooms;
  const Eigen::Vector3f slow_spot = kSlowSpotM / z13::station::kCellSize;
  std::vector<Stop> stops;
  AddStops(stops, "corridor", rooms::CorridorWalk(graph, slow_spot, kRouteCorridorRooms));
  AddStops(stops, "tour", rooms::TourViewpoints(graph, kRouteTourRooms));
  const std::vector<Eigen::Vector3f> inside {slow_spot};
  const std::vector<rooms::SpaceWalkStop> outside = rooms::SpaceWalkRoute(HullOf(blocks), inside);
  for (const rooms::SpaceWalkStop& stop : std::span(outside).subspan(1)) {
    stops.push_back({.label = stop.label, .eye = stop.eye, .target = stop.target});
  }
  return stops;
}

}  // namespace z13::testing
