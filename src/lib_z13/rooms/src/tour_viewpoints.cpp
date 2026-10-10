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

#include <rooms/tour_viewpoints.h>

#include <algorithm>
#include <array>
#include <iterator>
#include <optional>
#include <utility>

namespace z13::station::rooms {

namespace {

using z13::building::primitives::CellBox;

constexpr int kHeadings = 4;
// The eye stands one cell off both walls of the room's corner, or right in it.
const std::array kCornerOffsets {Eigen::Vector3i(1, 1, kViewpointEyeCells), Eigen::Vector3i(0, 0, kViewpointEyeCells)};
// The outside eye is this many station extents from its centre along each axis.
constexpr float kOutsideDistance = 0.75f;

Eigen::Vector3f CentreOf(const CellBox& box) {
  return (box.min.cast<float>() + box.End().cast<float>()) / 2.f;
}

// The anchor is the room's lowest cell, a corner of its floor.
std::optional<Eigen::Vector3f> EyeIn(const RoomGraph& graph, RoomIndex room) {
  for (const Eigen::Vector3i& offset : kCornerOffsets) {
    const Eigen::Vector3i cell = graph.rooms[room].anchor + offset;
    if (graph.RoomAt(cell) == room) {
      return cell.cast<float>() + Eigen::Vector3f::Constant(0.5f);
    }
  }
  return std::nullopt;
}

// Towards the room's centre first, then a quarter turn at a time.
void AddHeadings(RoomIndex room, const Eigen::Vector3f& eye, const Eigen::Vector3f& toward,
                 std::vector<Viewpoint>& viewpoints) {
  Eigen::Vector2f direction = (toward - eye).head<2>();
  direction = direction.isZero() ? Eigen::Vector2f::UnitX() : direction.normalized();
  for (int i = 0; i < kHeadings; ++i) {
    viewpoints.push_back({.room = room, .eye = eye, .target = eye + Eigen::Vector3f(direction.x(), direction.y(), 0.f)});
    direction = Eigen::Vector2f(-direction.y(), direction.x());
  }
}

// The largest window onto space of the room with the most window area onto space.
std::optional<size_t> LargestSpaceWindow(const RoomGraph& graph) {
  std::vector<int> area(graph.rooms.size());
  std::vector<std::optional<size_t>> largest(graph.rooms.size());
  for (size_t i = 0; i < graph.portals.size(); ++i) {
    const Portal& portal = graph.portals[i];
    // a < b, so the vacuum is always a.
    if (!portal.visible || portal.a != kVacuumRoom) {
      continue;
    }
    area[portal.b] += portal.area_cells;
    if (!largest[portal.b] || graph.portals[*largest[portal.b]].area_cells < portal.area_cells) {
      largest[portal.b] = i;
    }
  }
  const auto windowiest = std::ranges::max_element(area);
  if (windowiest == area.end() || *windowiest == 0) {
    return std::nullopt;
  }
  return largest[static_cast<size_t>(std::distance(area.begin(), windowiest))];
}

}  // namespace

std::vector<Viewpoint> TourViewpoints(const RoomGraph& graph, size_t rooms) {
  std::vector<std::pair<RoomIndex, Eigen::Vector3f>> candidates;
  for (RoomIndex room = kVacuumRoom + 1; room < graph.rooms.size(); ++room) {
    if (const auto eye = EyeIn(graph, room)) {
      candidates.emplace_back(room, *eye);
    }
  }
  // Rooms come sorted by anchor, so equal volumes keep one order on every run.
  std::ranges::stable_sort(candidates, {}, [&graph](const auto& candidate) {
    return graph.rooms[candidate.first].volume_cells;
  });

  std::vector<Viewpoint> viewpoints;
  const size_t picked = std::min(rooms, candidates.size());
  for (size_t i = 0; i < picked; ++i) {
    const size_t at = picked == 1 ? 0 : i * (candidates.size() - 1) / (picked - 1);
    const auto& [room, eye] = candidates[at];
    AddHeadings(room, eye, CentreOf(graph.rooms[room].bounds), viewpoints);
  }
  if (const auto window = LargestSpaceWindow(graph)) {
    const Portal& portal = graph.portals[*window];
    if (const auto eye = EyeIn(graph, portal.b)) {
      AddHeadings(portal.b, *eye, CentreOf(portal.opening), viewpoints);
    }
  }
  if (!graph.rooms.empty() && !graph.rooms[kVacuumRoom].bounds.extent.isZero()) {
    const CellBox& station = graph.rooms[kVacuumRoom].bounds;
    const Eigen::Vector3f centre = CentreOf(station);
    const Eigen::Vector3f away = station.extent.cast<float>().cwiseProduct(Eigen::Vector3f(-1.f, -1.f, 1.f));
    viewpoints.push_back({.room = kVacuumRoom, .eye = centre + (kOutsideDistance * away), .target = centre});
  }
  return viewpoints;
}

}  // namespace z13::station::rooms
