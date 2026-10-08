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

#include "room_overlay_system.h"

#include <algorithm>
#include <array>
#include <format>
#include <string>
#include <tuple>
#include <optional>
#include <vector>

#include <lib_core/state/world_state.h>
#include <lib_core/utils/math.h>
#include <lib_core/world/lifecycle.h>

#include <z13/components/gameplay.h>
#include <z13/components/rooms.h>
#include <z13/components/station.h>
#include <z13_rooms/room_cache.h>

namespace z13::station {

namespace {

using z13::station::rooms::RoomCache;
using z13::station::rooms::RoomGraph;
using z13::station::rooms::RoomIndex;

using PlayerQuery = flecs::query<const z13::gameplay::Player, const Eigen::Matrix4f>;

constexpr int kProbeCellsAbove = 4;

constexpr std::array<uint8_t, 4> kRoomColor {80, 200, 120, 255};
constexpr std::array<uint8_t, 4> kWindowColor {80, 160, 255, 255};
constexpr std::array<uint8_t, 4> kDoorColor {255, 160, 60, 255};

std::optional<Eigen::Vector3i> PlayerCell(const PlayerQuery& players, const z13::gameplay::LocalPlayer& local) {
  std::optional<Eigen::Vector3i> cell;
  if (!local.id) {
    return cell;
  }
  players.each([&](const z13::gameplay::Player& player, const Eigen::Matrix4f& transform) {
    if (player.id == *local.id) {
      const Eigen::Vector3f position = z13::math::ExtractTranslation<float>(transform);
      cell = (position / kCellSize).array().floor().cast<int>();
    }
  });
  return cell;
}

OverlayBox Box(const Eigen::Vector3i& min, const Eigen::Vector3i& extent, const std::array<uint8_t, 4>& color) {
  return {.min = min, .extent = extent, .color = color};
}

// The room's cells in one column interval.
OverlayBox RunOf(int x, int y, const rooms::FreeInterval& interval) {
  return Box({x, y, interval.begin}, {1, 1, interval.end - interval.begin}, kRoomColor);
}

// Joins the boxes that touch face to face along `axis` with the same footprint across it.
bool MergeAlong(std::vector<OverlayBox>& boxes, Eigen::Index axis) {
  const Eigen::Index u = (axis + 1) % 3;
  const Eigen::Index v = (axis + 2) % 3;
  const auto key = [&](const OverlayBox& box) {
    return std::tuple(box.min[u], box.extent[u], box.min[v], box.extent[v], box.min[axis]);
  };
  std::ranges::sort(boxes, {}, key);
  std::vector<OverlayBox> merged;
  for (const OverlayBox& box : boxes) {
    if (!merged.empty()) {
      OverlayBox& last = merged.back();
      if (last.min[u] == box.min[u] && last.extent[u] == box.extent[u] && last.min[v] == box.min[v] &&
          last.extent[v] == box.extent[v] && last.min[axis] + last.extent[axis] == box.min[axis]) {
        last.extent[axis] += box.extent[axis];
        continue;
      }
    }
    merged.push_back(box);
  }
  const bool joined = merged.size() < boxes.size();
  boxes = std::move(merged);
  return joined;
}

// Fewest boxes the greedy joins reach, so the walls between neighbouring boxes of one room
// don't show.
void MergeBoxes(std::vector<OverlayBox>& boxes) {
  bool joined = true;
  while (joined) {
    joined = false;
    for (Eigen::Index axis = 0; axis < 3; ++axis) {
      joined = MergeAlong(boxes, axis) || joined;
    }
  }
}

// The room's cells as boxes: one per column interval, then joined.
void AddRoomBoxes(const RoomGraph& graph, RoomIndex room, std::vector<OverlayBox>& boxes) {
  const auto& bounds = graph.rooms[room].bounds;
  const size_t first = boxes.size();
  for (int y = bounds.min.y(); y < bounds.End().y(); ++y) {
    for (int x = bounds.min.x(); x < bounds.End().x(); ++x) {
      for (const auto& interval : graph.columns.Column({x, y})) {
        if (interval.room == room) {
          boxes.push_back(RunOf(x, y, interval));
        }
      }
    }
  }
  std::vector<OverlayBox> room_boxes(boxes.begin() + static_cast<std::ptrdiff_t>(first), boxes.end());
  boxes.resize(first);
  MergeBoxes(room_boxes);
  boxes.insert(boxes.end(), room_boxes.begin(), room_boxes.end());
}

// The room at the cell, or the first free cell above it: a player resting on the floor can
// have its position inside the floor's top cell.
std::optional<RoomIndex> RoomNear(const RoomGraph& graph, Eigen::Vector3i cell) {
  for (int i = 0; i < kProbeCellsAbove; ++i, ++cell.z()) {
    if (const auto room = graph.RoomAt(cell)) {
      return room;
    }
  }
  return std::nullopt;
}

// What the overlay shows for the room the player is in, or for none.
void Describe(const RoomGraph& graph, std::optional<RoomIndex> room, RoomOverlay& overlay) {
  overlay.boxes.clear();
  if (!room) {
    overlay.label = "Inside a block";
    return;
  }
  if (*room != rooms::kVacuumRoom) {
    AddRoomBoxes(graph, *room, overlay.boxes);
  }
  int portals {};
  for (const rooms::Portal& portal : graph.portals) {
    if (portal.a == *room || portal.b == *room) {
      overlay.boxes.push_back(Box(portal.opening.min, portal.opening.extent, portal.visible ? kWindowColor : kDoorColor));
      ++portals;
    }
  }
  overlay.label = *room == rooms::kVacuumRoom
                      ? std::format("Open to space, {} portals", portals)
                      : std::format("Room {}: {} cells, {} portals", *room, graph.rooms[*room].volume_cells, portals);
}

void UpdateOverlay(
    RoomOverlay& overlay, const RoomCache& cache, const z13::gameplay::LocalPlayer& local, const PlayerQuery& players) {
  const auto cell = PlayerCell(players, local);
  if (!overlay.enabled || !cache.Current() || !cell) {
    overlay.boxes.clear();
    overlay.label.clear();
    overlay.shown_fingerprint.reset();
    return;
  }
  const RoomGraph& graph = cache.Current()->get();
  const std::optional<RoomIndex> room = RoomNear(graph, *cell);
  if (overlay.shown_fingerprint == cache.CurrentFingerprint() && overlay.shown_room == room) {
    return;
  }
  Describe(graph, room, overlay);
  overlay.shown_fingerprint = cache.CurrentFingerprint();
  overlay.shown_room = room;
}

void RegisterComponents(flecs::world world) {
  z13::flecs_tools::RegisterComponent<RoomOverlay>(world);
}

void RegisterSystems(flecs::world world) {
  world.set(RoomOverlay {});
  const PlayerQuery players =
      world.query_builder<const z13::gameplay::Player, const Eigen::Matrix4f>("RoomOverlaySystem::Players").build();
  world.system<RoomOverlay, const RoomCache, const z13::gameplay::LocalPlayer>("RoomOverlaySystem::Update")
      .kind<StationTopologyPhase>()
      .with<StationMode>()
      .each([players](flecs::iter&, size_t, RoomOverlay& overlay, const RoomCache& cache,
                      const z13::gameplay::LocalPlayer& local) { UpdateOverlay(overlay, cache, local, players); });
}

}  // namespace

void RoomOverlaySystem::Register(flecs::world& world) {
  OnRegisterComponents(world, RegisterComponents);

  OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::station
