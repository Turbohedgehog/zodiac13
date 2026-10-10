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
#include <format>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <lib_core/state/world_state.h>
#include <lib_core/utils/math.h>
#include <lib_core/world/lifecycle.h>

#include <z13/components/gameplay.h>
#include <z13/components/rooms.h>
#include <z13/components/station.h>
#include <rooms/room_cache.h>

#include "room_box_cache.h"

namespace z13::station {

namespace {

using z13::station::rooms::RoomCache;
using z13::station::rooms::RoomGraph;
using z13::station::rooms::RoomIndex;

using PlayerQuery = flecs::query<const z13::gameplay::Player, const Eigen::Matrix4f>;

constexpr int kProbeCellsAbove = 4;

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

std::string Label(const RoomGraph& graph, const std::optional<Eigen::Vector3i>& cell, std::optional<RoomIndex> room) {
  if (!cell) {
    return {};
  }
  if (!room) {
    return "Inside a block";
  }
  const auto portals = std::ranges::count_if(
      graph.portals, [&room](const rooms::Portal& portal) { return portal.a == *room || portal.b == *room; });
  return *room == rooms::kVacuumRoom
             ? std::format("Open to space, {} portals", portals)
             : std::format("Room {}: {} cells, {} portals", *room, graph.rooms[*room].volume_cells, portals);
}

OverlayBox PortalBox(const rooms::Portal& portal) {
  return {.min = portal.opening.min,
          .extent = portal.opening.extent,
          .color = portal.visible ? kWindowOverlayColor : kDoorOverlayColor};
}

std::vector<OverlayBox> Boxes(const RoomGraph& graph, const RoomOverlayShown& shown, RoomBoxCache& box_cache) {
  std::vector<OverlayBox> boxes;
  const auto add_room = [&](RoomIndex room) {
    if (room != rooms::kVacuumRoom && room < graph.rooms.size()) {
      const std::vector<OverlayBox>& room_boxes = box_cache.BoxesOf(graph, shown.fingerprint, room);
      boxes.insert(boxes.end(), room_boxes.begin(), room_boxes.end());
    }
  };
  if (shown.mode == RoomOverlayMode::kSeen && shown.seen_rooms) {
    std::ranges::for_each(*shown.seen_rooms, add_room);
  } else if (shown.mode == RoomOverlayMode::kAll) {
    for (RoomIndex room = 0; room < graph.rooms.size(); ++room) {
      add_room(room);
    }
    std::ranges::transform(graph.portals, std::back_inserter(boxes), PortalBox);
  }
  return boxes;
}

// Rooms drawn against an older graph are numbered differently, so they wait for the renderer.
std::optional<std::vector<uint32_t>> DrawnRooms(const RoomOverlay& overlay, uint64_t fingerprint) {
  if (overlay.mode != RoomOverlayMode::kSeen || !overlay.drawn || overlay.drawn->fingerprint != fingerprint) {
    return std::nullopt;
  }
  return overlay.drawn->rooms;
}

void UpdateOverlay(RoomOverlay& overlay, RoomBoxCache& box_cache, const RoomCache& cache,
                   const z13::gameplay::LocalPlayer& local, const PlayerQuery& players) {
  const auto fingerprint = cache.CurrentFingerprint();
  if (overlay.mode == RoomOverlayMode::kOff || !cache.Current() || !fingerprint) {
    overlay = {.mode = overlay.mode, .drawn = std::move(overlay.drawn)};
    return;
  }
  const RoomGraph& graph = cache.Current()->get();
  const auto cell = PlayerCell(players, local);
  const std::optional<RoomIndex> room = cell ? RoomNear(graph, *cell) : std::nullopt;
  RoomOverlayShown wanted {
      .mode = overlay.mode,
      .fingerprint = *fingerprint,
      .player_room = room,
      .seen_rooms = DrawnRooms(overlay, *fingerprint),
  };
  if (overlay.shown == wanted) {
    return;
  }
  overlay.boxes = Boxes(graph, wanted, box_cache);
  overlay.label = Label(graph, cell, room);
  overlay.shown = std::move(wanted);
}

void RegisterComponents(flecs::world world) {
  z13::flecs_tools::RegisterComponents<RoomOverlay, RoomBoxCache>(world);
}

void RegisterSystems(flecs::world world) {
  world.set(RoomOverlay {});
  world.set(RoomBoxCache {});
  const PlayerQuery players =
      world.query_builder<const z13::gameplay::Player, const Eigen::Matrix4f>("RoomOverlaySystem::Players").build();
  world.system<RoomOverlay, RoomBoxCache, const RoomCache, const z13::gameplay::LocalPlayer>("RoomOverlaySystem::Update")
      .kind<StationTopologyPhase>()
      .with<StationMode>()
      .each([players](flecs::iter&, size_t, RoomOverlay& overlay, RoomBoxCache& box_cache, const RoomCache& cache,
                      const z13::gameplay::LocalPlayer& local) {
        UpdateOverlay(overlay, box_cache, cache, local, players);
      });
}

}  // namespace

void RoomOverlaySystem::Register(flecs::world& world) {
  OnRegisterComponents(world, RegisterComponents);

  OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::station