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
#include <memory>
#include <unordered_map>
#include <format>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <lib_core/state/world_state.h>
#include <lib_core/utils/math.h>
#include <lib_core/world/lifecycle.h>
#include <lib_profile/profile.h>

#include <z13/components/station/overlay_layers.h>
#include <z13/components/gameplay/player.h>
#include <z13/components/station/rooms.h>
#include <z13/components/station/block.h>
#include <z13/components/station/grid.h>
#include <z13/components/station/grid_pose.h>
#include <z13/components/station/station_mode.h>
#include <grid/block_index.h>
#include <primitives/palette.h>
#include <rooms/room_cache.h>

#include <outline/room_box_cache.h>
#include <outline/room_outline.h>

namespace z13::station {

namespace {

using z13::station::rooms::RoomCache;
using z13::station::rooms::RoomGraph;
using z13::station::rooms::RoomId;

using z13::building::grid::BlockId;
using z13::building::grid::BlockIndex;
using z13::building::primitives::BlockPalette;

using PlayerQuery = flecs::query<const z13::gameplay::Player, const Eigen::Matrix4f>;

std::optional<Block> BlockOf(const flecs::world& world, BlockId id) {
  if (!world.is_alive(id)) {
    return std::nullopt;
  }
  const Block* block = world.entity(id).try_get<Block>();
  return block != nullptr ? std::optional(*block) : std::nullopt;
}

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
std::optional<RoomId> RoomNear(const RoomGraph& graph, Eigen::Vector3i cell) {
  for (int i = 0; i < kProbeCellsAbove; ++i, ++cell.z()) {
    if (const auto room = graph.RoomAt(cell)) {
      return room;
    }
  }
  return std::nullopt;
}

std::string Label(const RoomGraph& graph, const std::optional<Eigen::Vector3i>& cell, std::optional<RoomId> room) {
  if (!cell) {
    return {};
  }
  if (!room) {
    return "Inside a block";
  }
  const auto portals = std::ranges::count_if(
      graph.portals, [&room](const rooms::Portal& portal) { return portal.a == *room || portal.b == *room; });
  const auto found = graph.FindRoom(*room);
  if (*room == rooms::kVacuumRoom || !found) {
    return std::format("Open to space, {} portals", portals);
  }
  return std::format("Room {}: {} cells, {} portals", *room, found->get().volume_cells, portals);
}

OverlayBox PortalBox(const rooms::Portal& portal, const Rgba& color) {
  return {.min = portal.opening.min, .extent = portal.opening.extent, .color = color};
}

OverlayBox PortalBoxByKind(const rooms::Portal& portal) {
  return PortalBox(portal, portal.visible ? kWindowOverlayColor : kDoorOverlayColor);
}

// The visible portals with a seen room on both sides.
void AddSeenPortals(const RoomGraph& graph, const std::vector<uint32_t>& seen, std::vector<OverlayBox>& boxes) {
  for (const rooms::Portal& portal : graph.portals) {
    if (portal.visible && std::ranges::binary_search(seen, portal.a) && std::ranges::binary_search(seen, portal.b)) {
      boxes.push_back(PortalBox(portal, kSeenPortalOverlayColor));
    }
  }
}

struct OverlayLines {
  std::vector<OverlayBox> boxes;
  std::vector<OverlaySegment> segments;
  std::vector<uint32_t> outlined_rooms;
};

OverlayLines Boxes(const RoomCache& cache, const RoomOverlayShown& shown, RoomBoxCache& box_cache,
                   const WallSource& source) {
  Z13_PROFILE_ZONE("RoomOverlay::Boxes");
  const RoomGraph& graph = cache.Current()->get();
  OverlayLines lines;
  std::vector<OverlayBox>& boxes = lines.boxes;
  const Rgba room_color = shown.mode == RoomOverlayMode::kSeen ? kSeenRoomOverlayColor : kRoomOverlayColor;
  const auto add_room = [&](RoomId room) {
    if (room != rooms::kVacuumRoom && graph.rooms.contains(room)) {
      const RoomOverlayShape& shape = box_cache.ShapeOf(cache, room, source);
      if (!shape.segments.empty()) {
        lines.outlined_rooms.push_back(room);
      }
      std::ranges::transform(shape.boxes, std::back_inserter(boxes), [&room_color](OverlayBox box) {
        box.color = room_color;
        return box;
      });
      std::ranges::transform(shape.segments, std::back_inserter(lines.segments), [&room_color](OverlaySegment segment) {
        segment.color = room_color;
        return segment;
      });
    }
  };
  if (shown.mode == RoomOverlayMode::kSeen && shown.seen_rooms) {
    std::ranges::for_each(*shown.seen_rooms, add_room);
    AddSeenPortals(graph, *shown.seen_rooms, boxes);
  } else if (shown.mode == RoomOverlayMode::kAll) {
    std::ranges::for_each(graph.ByAnchor(), add_room);
    std::ranges::transform(graph.portals, std::back_inserter(boxes), PortalBoxByKind);
  }
  return lines;
}

using DistrictCaches = std::unordered_map<GridId, RoomBoxCache>;

// The corners of a box, joined by the edges of a cube.
constexpr std::array<std::array<int, 2>, 12> kCubeEdges {{{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3},
                                                          {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}}};

Eigen::Vector3f ToMainCells(const Eigen::Isometry3f& pose, const Eigen::Vector3f& cells) {
  return (pose * (cells * kCellSize)) / kCellSize;
}

// A district's box has no place among the main grid's axis-aligned ones, so it is drawn as the edges of its cube.
void AddBoxEdges(const OverlayBox& box, const Eigen::Isometry3f& pose, std::vector<OverlaySegment>& segments) {
  std::array<Eigen::Vector3f, 8> corners;
  for (int i = 0; i < 8; ++i) {
    const Eigen::Vector3f offset((i & 1) != 0 ? 1.f : 0.f, (i & 2) != 0 ? 1.f : 0.f, (i & 4) != 0 ? 1.f : 0.f);
    corners[i] = ToMainCells(pose, box.min.cast<float>() + offset.cwiseProduct(box.extent.cast<float>()));
  }
  for (const auto& [from, to] : kCubeEdges) {
    segments.push_back({.a = corners[from], .b = corners[to], .color = box.color});
  }
}

// Every other grid's rooms and portals, as lines in the main grid's cells.
void AddDistrictLines(const flecs::world& world, const BlockPalette& palette, DistrictCaches& caches,
                      OverlayLines& lines) {
  world.query<const Grid, const RoomCache, const BlockIndex>().each(
      [&](const Grid& grid, const RoomCache& cache, const BlockIndex& index) {
        if (grid.id == kMainGrid || !cache.Current()) {
          return;
        }
        const WallSource source {.index = index, .palette = palette.palette, .block_of = [world](BlockId id) {
                                   return BlockOf(world, id);
                                 }};
        const Eigen::Isometry3f pose = GridToWorld(grid);
        OverlayLines local = Boxes(cache, {.mode = RoomOverlayMode::kAll}, caches[grid.id], source);
        for (const OverlayBox& box : local.boxes) {
          AddBoxEdges(box, pose, lines.segments);
        }
        for (OverlaySegment segment : local.segments) {
          segment.a = ToMainCells(pose, segment.a);
          segment.b = ToMainCells(pose, segment.b);
          lines.segments.push_back(segment);
        }
      });
}

// Rooms drawn against an older graph are numbered differently, so they wait for the renderer.
std::optional<std::vector<uint32_t>> DrawnRooms(const RoomOverlay& overlay, uint64_t fingerprint) {
  if (overlay.mode != RoomOverlayMode::kSeen || !overlay.drawn || overlay.drawn->fingerprint != fingerprint) {
    return std::nullopt;
  }
  return overlay.drawn->rooms;
}

void UpdateOverlay(RoomOverlay& overlay, RoomBoxCache& box_cache, const RoomCache& cache,
                   const z13::gameplay::LocalPlayer& local, const PlayerQuery& players, const WallSource& source,
                   const flecs::world& world, const BlockPalette& palette, DistrictCaches& districts) {
  const auto fingerprint = cache.CurrentFingerprint();
  if (overlay.mode == RoomOverlayMode::kOff || !cache.Current() || !fingerprint) {
    overlay = {.mode = overlay.mode, .drawn = std::move(overlay.drawn)};
    return;
  }
  const RoomGraph& graph = cache.Current()->get();
  const auto cell = PlayerCell(players, local);
  const std::optional<RoomId> room = cell ? RoomNear(graph, *cell) : std::nullopt;
  RoomOverlayShown wanted {
      .mode = overlay.mode,
      .fingerprint = *fingerprint,
      .player_room = room,
      .seen_rooms = DrawnRooms(overlay, *fingerprint),
  };
  if (overlay.mode == RoomOverlayMode::kAll) {
    world.query<const Grid, const RoomCache>().each([&](const Grid& grid, const RoomCache& other) {
      if (grid.id != kMainGrid) {
        wanted.district_fingerprints.push_back(other.CurrentFingerprint().value_or(0));
      }
    });
  }
  if (overlay.shown == wanted) {
    return;
  }
  OverlayLines lines = Boxes(cache, wanted, box_cache, source);
  if (overlay.mode == RoomOverlayMode::kAll) {
    AddDistrictLines(world, palette, districts, lines);
  }
  overlay.boxes = std::move(lines.boxes);
  overlay.segments = std::move(lines.segments);
  overlay.outlined_rooms = std::move(lines.outlined_rooms);
  overlay.label = Label(graph, cell, room);
  overlay.shown = std::move(wanted);
}

void RegisterComponents(flecs::world world) {
  z13::flecs_tools::RegisterComponents<RoomOverlay, RoomBoxCache, OverlayLayers>(world);
}

void RegisterSystems(flecs::world world) {
  world.set(RoomOverlay {});
  world.set(RoomBoxCache {});
  world.set(OverlayLayers {});
  const auto districts = std::make_shared<DistrictCaches>();
  const PlayerQuery players =
      world.query_builder<const z13::gameplay::Player, const Eigen::Matrix4f>("RoomOverlaySystem::Players").build();
  world.system<RoomOverlay, RoomBoxCache, const z13::gameplay::LocalPlayer, const BlockPalette>(
           "RoomOverlaySystem::Update")
      .kind<StationTopologyPhase>()
      .with<StationMode>()
      .read<Block>()
      .read<BlockIndex>()
      .read<RoomCache>()
      .each([players, districts](flecs::iter& it, size_t, RoomOverlay& overlay, RoomBoxCache& box_cache,
                      const z13::gameplay::LocalPlayer& local, const BlockPalette& palette) {
        const flecs::world world = it.world();
        const auto main_index = MainGridData<BlockIndex>(world);
        const auto main_cache = MainGridData<RoomCache>(world);
        if (!main_index || !main_cache) {
          return;
        }
        const RoomCache& cache = main_cache->get();
        const WallSource source {.index = *main_index, .palette = palette.palette, .block_of = [world](BlockId id) {
                                   return BlockOf(world, id);
                                 }};
        UpdateOverlay(overlay, box_cache, cache, local, players, source, world, palette, *districts);
      });
}

}  // namespace

void RoomOverlaySystem::Register(flecs::world& world) {
  OnRegisterComponents(world, RegisterComponents);

  OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::station