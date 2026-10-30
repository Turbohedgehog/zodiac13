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

#include "room_outline.h"

#include <algorithm>
#include <array>
#include <span>
#include <unordered_map>
#include <unordered_set>

#include "outline_edges.h"
#include "room_boundary.h"
#include "wall_faces.h"

namespace z13::station {

namespace {

using namespace outline;
using z13::building::grid::BlockId;
using z13::building::primitives::CellBox;

constexpr int kAxes = z13::building::primitives::kCellAxes;
// A free cell whose centre lies this near a wall's plane is on the room's side of it.
constexpr float kOnPlane = 1e-3f;

const std::array<Cell, 4> kHorizontalSteps {Cell {1, 0, 0}, Cell {-1, 0, 0}, Cell {0, 1, 0}, Cell {0, -1, 0}};

bool PassesThrough(const Face& face, const Cell& cell) {
  Polygon column = face.polygon;
  for (int axis = 0; axis < kVerticalAxis; ++axis) {
    Point unit = Point::Zero();
    unit[axis] = 1.0f;
    column = KeepAbove(column, unit, static_cast<float>(cell[axis]));
    column = KeepAbove(column, -unit, -static_cast<float>(cell[axis] + 1));
  }
  return Solid(std::move(column)).has_value();
}

// The floor or ceiling under an oblique wall's cell next to the room runs up to the wall's face.
bool AddFloorToWall(const Cell& floor_cell, int level, const Cell& towards_room, const BlockFaces& wall,
                    OutlineEdges& edges) {
  const Point towards = towards_room.cast<float>();
  const auto z = static_cast<float>(level);
  const auto x = static_cast<float>(floor_cell.x());
  const auto y = static_cast<float>(floor_cell.y());
  Polygon square {{x, y, z}, {x + 1, y, z}, {x + 1, y + 1, z}, {x, y + 1, z}};
  bool cut = false;
  for (const Face& face : wall.faces) {
    if (face.normal.dot(towards) > kFacing && PassesThrough(face, floor_cell)) {
      square = KeepAbove(square, face.normal, face.normal.dot(face.polygon.front()));
      cut = true;
    }
  }
  const auto kept = cut ? Solid(std::move(square)) : std::nullopt;
  if (kept) {
    edges.AddPolygon(*kept, Point::UnitZ());
  }
  return kept.has_value();
}

void AddSquareUpToWalls(const Cell& cell, int level, std::span<const std::reference_wrapper<const BlockFaces>> walls,
                        OutlineEdges& edges) {
  const Point centre {static_cast<float>(cell.x()) + 0.5f, static_cast<float>(cell.y()) + 0.5f,
                      static_cast<float>(level)};
  const auto x = static_cast<float>(cell.x());
  const auto y = static_cast<float>(cell.y());
  const auto z = static_cast<float>(level);
  Polygon square {{x, y, z}, {x + 1, y, z}, {x + 1, y + 1, z}, {x, y + 1, z}};
  for (const BlockFaces& wall : walls) {
    for (const Face& face : wall.faces) {
      const float plane = face.normal.dot(face.polygon.front());
      if (face.normal.dot(centre) > plane - kOnPlane && PassesThrough(face, cell)) {
        square = KeepAbove(square, face.normal, plane);
      }
    }
  }
  const auto kept = Solid(std::move(square));
  if (!kept) {
    return;
  }
  if (AreaOf(*kept) > 1.0f - kMinArea) {
    edges.AddCellFace(cell, kVerticalAxis, level != cell.z());
  } else {
    edges.AddPolygon(*kept, Point::UnitZ());
  }
}

// The floor or ceiling runs on under the oblique block's cells for as long as it is on the room's side of the wall.
void ExtendFloor(RoomBoundary& boundary, const Cell& start, int level, const Cell& towards_room,
                 std::unordered_set<FloorSpot, FloorSpotHash>& done, OutlineEdges& edges) {
  const auto wall = boundary.ObliqueAt(start);
  if (!wall || boundary.InRoom(start)) {
    return;
  }
  std::vector<Cell> pending {start};
  while (!pending.empty()) {
    const Cell next = pending.back();
    pending.pop_back();
    if (!done.insert({next, level}).second || !AddFloorToWall(next, level, towards_room, wall->second, edges)) {
      continue;
    }
    for (const Cell& side : kHorizontalSteps) {
      const auto neighbour = boundary.ObliqueAt(next + side);
      if (neighbour && neighbour->first == wall->first && !boundary.InRoom(next + side)) {
        pending.push_back(next + side);
      }
    }
  }
}

}  // namespace

std::optional<std::vector<OverlaySegment>> WallOutline(
    const z13::station::rooms::RoomGraph& graph, z13::station::rooms::RoomId room, const WallSource& source,
    const Rgba& color) {
  RoomBoundary boundary(graph, room, source);
  OutlineEdges edges;
  std::unordered_map<BlockId, std::vector<Cell>> walls;
  std::unordered_set<FloorSpot, FloorSpotHash> floors;
  for (const auto& leaf_ref : graph.space.LeavesIn(graph.rooms.at(room).bounds)) {
    if (graph.RoomOf(leaf_ref) != room) {
      continue;
    }
    const CellBox& leaf = graph.space.Leaf(leaf_ref);
    for (int axis = 0; axis < kAxes; ++axis) {
      const int u = (axis + 1) % kAxes;
      const int v = (axis + 2) % kAxes;
      for (const bool far_side : {false, true}) {
        Cell step = Cell::Zero();
        step[axis] = far_side ? 1 : -1;
        for (int i = 0; i < leaf.extent[u]; ++i) {
          for (int j = 0; j < leaf.extent[v]; ++j) {
            Cell cell = leaf.min;
            cell[axis] += far_side ? leaf.extent[axis] - 1 : 0;
            cell[u] += i;
            cell[v] += j;
            const Cell beyond = cell + step;
            if (boundary.InRoom(beyond)) {
              continue;
            }
            if (const auto wall = boundary.ObliqueAt(beyond)) {
              const Cell way = -step;
              auto& ways = walls[wall->first];
              if (std::ranges::find(ways, way) == ways.end()) {
                ways.push_back(way);
              }
              continue;
            }
            if (axis == kVerticalAxis && !boundary.ObliqueAround(cell).empty()) {
              AddSquareUpToWalls(cell, cell.z() + (far_side ? 1 : 0), boundary.ObliqueAround(cell), edges);
            } else {
              edges.AddCellFace(cell, axis, far_side);
            }
            if (axis != kVerticalAxis) {
              continue;
            }
            for (const Cell& side : kHorizontalSteps) {
              const int level = cell.z() + (far_side ? 1 : 0);
              ExtendFloor(boundary, cell + side, level, -side, floors, edges);
            }
          }
        }
      }
    }
  }
  if (walls.empty()) {
    return std::nullopt;
  }
  for (const auto& [id, wall] : walls) {
    for (const Face& face : boundary.FacesOfBlock(id).faces) {
      const bool facing = std::ranges::any_of(
          wall, [&face](const Cell& way) { return face.normal.dot(way.cast<float>()) > kFacing; });
      if (facing) {
        edges.AddPolygon(face.polygon, face.normal);
      }
    }
  }
  return edges.Segments(color);
}

}  // namespace z13::station
