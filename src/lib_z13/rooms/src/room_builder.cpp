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


#include <rooms/room_builder.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include <numeric>
#include <unordered_map>
#include <utility>

#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/connected_components.hpp>

#include "room_builder_types.h"

namespace z13::station::rooms {

namespace {

using z13::building::primitives::CellBox;
using LeafGraph = boost::adjacency_list<boost::vecS, boost::vecS, boost::undirectedS>;

// Every leaf of the free space, numbered chunk by chunk in OrderedChunks().
struct NumberedLeaves {
  std::vector<LeafRef> leaves;
  std::unordered_map<Eigen::Vector3i, size_t, z13::building::primitives::CellHash> first_of_chunk;

  size_t Number(const LeafRef& ref) const { return first_of_chunk.at(ref.chunk) + ref.leaf; }
};

NumberedLeaves NumberLeaves(const FreeSpace& space) {
  NumberedLeaves numbered;
  for (const Eigen::Vector3i& key : space.OrderedChunks()) {
    numbered.first_of_chunk.emplace(key, numbered.leaves.size());
    const auto count = static_cast<uint32_t>(space.AllChunks().at(key)->leaves.size());
    for (uint32_t leaf = 0; leaf < count; ++leaf) {
      numbered.leaves.push_back({.chunk = key, .leaf = leaf});
    }
  }
  return numbered;
}

// Per leaf, its connected set; the leaves open to space join the vertex after the last leaf.
std::vector<size_t> ConnectedSets(const FreeSpace& space, const NumberedLeaves& numbered) {
  const size_t vacuum = numbered.leaves.size();
  LeafGraph graph(vacuum + 1);
  for (size_t i = 0; i < numbered.leaves.size(); ++i) {
    if (space.OnOuterFace(space.Leaf(numbered.leaves[i]))) {
      boost::add_edge(i, vacuum, graph);
    }
    for (const LeafRef& neighbour : space.Neighbours(numbered.leaves[i])) {
      if (const size_t j = numbered.Number(neighbour); j > i) {
        boost::add_edge(i, j, graph);
      }
    }
  }
  std::vector<size_t> set_of(vacuum + 1);
  boost::connected_components(graph, set_of.data());
  return set_of;
}

void Absorb(RoomExtent& room, const CellBox& leaf) {
  if (AnchorBefore(leaf.min, room.anchor)) {
    room.anchor = leaf.min;
  }
  room.low = room.low.cwiseMin(leaf.min);
  room.high = room.high.cwiseMax(leaf.End());
  room.volume += leaf.extent.cast<int64_t>().prod();
}

Room ToRoom(const RoomExtent& extent) {
  return {.anchor = extent.anchor,
          .volume_cells = extent.volume,
          .bounds = {.min = extent.low, .extent = extent.high - extent.low}};
}

// Names the connected sets: the vacuum's set is room 0, the rest get ids by anchor from
// `first_id`. Fills the graph's rooms and the room of every leaf.
void NameRooms(const NumberedLeaves& numbered, const std::vector<size_t>& set_of, RoomId first_id, RoomGraph& graph) {
  const size_t vacuum_set = set_of.back();
  std::unordered_map<size_t, RoomExtent> extents;
  for (size_t i = 0; i < numbered.leaves.size(); ++i) {
    if (set_of[i] == vacuum_set) {
      continue;
    }
    const CellBox& leaf = graph.space.Leaf(numbered.leaves[i]);
    auto& extent = extents.try_emplace(set_of[i], RoomExtent {.anchor = leaf.min, .low = leaf.min, .high = leaf.End()})
                       .first->second;
    Absorb(extent, leaf);
  }
  std::vector<std::pair<size_t, RoomExtent>> by_anchor(extents.begin(), extents.end());
  std::ranges::sort(by_anchor,
                    [](const auto& a, const auto& b) { return AnchorBefore(a.second.anchor, b.second.anchor); });
  std::unordered_map<size_t, RoomId> id_of_set {{vacuum_set, kVacuumRoom}};
  graph.rooms.emplace(kVacuumRoom, Room {.bounds = graph.space.Bounds()});
  RoomId next_id = first_id;
  for (const auto& [set, extent] : by_anchor) {
    id_of_set.emplace(set, next_id);
    graph.rooms.emplace(next_id++, ToRoom(extent));
  }
  graph.next_id = next_id;
  for (const auto& [key, chunk] : graph.space.AllChunks()) {
    std::vector<RoomId> rooms(chunk->leaves.size());
    const size_t first = numbered.first_of_chunk.at(key);
    for (size_t leaf = 0; leaf < rooms.size(); ++leaf) {
      rooms[leaf] = id_of_set.at(set_of[first + leaf]);
    }
    graph.leaf_rooms.emplace(key, std::make_shared<const std::vector<RoomId>>(std::move(rooms)));
  }
}

// The leaves in `side`, one cell thick across `axis`, as patches on the face.
std::vector<FacePatch> PatchesOn(const RoomGraph& graph, const CellBox& side, size_t axis) {
  const auto u = static_cast<Eigen::Index>((axis + 1) % 3);
  const auto v = static_cast<Eigen::Index>((axis + 2) % 3);
  std::vector<FacePatch> patches;
  for (const LeafRef& ref : graph.space.LeavesIn(side)) {
    const CellBox cut = graph.space.Leaf(ref).Intersection(side);
    patches.push_back(
        {.min = {cut.min[u], cut.min[v]}, .end = {cut.End()[u], cut.End()[v]}, .room = graph.RoomOf(ref)});
  }
  return patches;
}

int OverlapArea(const FacePatch& a, const FacePatch& b) {
  const Eigen::Vector2i size = a.end.cwiseMin(b.end) - a.min.cwiseMax(b.min);
  return (size.array() > 0).all() ? size.x() * size.y() : 0;
}

// The rooms on the two sides of each opening's face, counted per pair of rooms by the
// cells where the leaves on both sides meet.
void AddPortals(const RoomSources& sources, RoomGraph& graph) {
  std::vector<PortalSource> ordered = sources.portals;
  std::ranges::sort(ordered, [](const PortalSource& a, const PortalSource& b) {
    return AnchorBefore(a.opening.min, b.opening.min);
  });
  for (const PortalSource& source : ordered) {
    const auto axis = static_cast<size_t>(source.axis);
    const auto index = static_cast<Eigen::Index>(axis);
    CellBox before = source.opening;
    before.extent[index] = 1;
    --before.min[index];
    CellBox after = before;
    after.min[index] += source.opening.extent[index] + 1;
    const std::vector<FacePatch> low_patches = PatchesOn(graph, before, axis);
    const std::vector<FacePatch> high_patches = PatchesOn(graph, after, axis);
    std::vector<OpeningShare> shares;
    for (const FacePatch& low : low_patches) {
      for (const FacePatch& high : high_patches) {
        const int area = OverlapArea(low, high);
        if (area == 0 || low.room == high.room) {
          continue;
        }
        const std::pair pair {std::min(low.room, high.room), std::max(low.room, high.room)};
        const auto found = std::ranges::find(shares, pair, &OpeningShare::rooms);
        if (found == shares.end()) {
          shares.push_back({.rooms = pair, .area_cells = area, .low_side = low.room});
        } else {
          found->area_cells += area;
        }
      }
    }
    std::ranges::sort(shares, {}, &OpeningShare::rooms);
    for (const OpeningShare& share : shares) {
      graph.portals.push_back({.a = share.rooms.first,
                               .b = share.rooms.second,
                               .area_cells = share.area_cells,
                               .visible = source.visible,
                               .passable = source.passable,
                               .opening = source.opening,
                               .axis = source.axis,
                               .low_side = share.low_side});
    }
  }
}

std::unordered_map<RoomId, std::vector<size_t>> PortalsByRoom(const RoomGraph& graph) {
  std::unordered_map<RoomId, std::vector<size_t>> by_room;
  for (size_t i = 0; i < graph.portals.size(); ++i) {
    by_room[graph.portals[i].a].push_back(i);
    by_room[graph.portals[i].b].push_back(i);
  }
  return by_room;
}

}  // namespace

std::expected<RoomGraph, std::string> BuildRooms(const RoomSources& sources, RoomId first_id, int chunk_cells) {
  auto space = FreeSpace::Build(sources.sealed, chunk_cells);
  if (!space) {
    return std::unexpected(std::move(space.error()));
  }
  RoomGraph graph {.space = std::move(*space), .next_id = first_id};
  const NumberedLeaves numbered = NumberLeaves(graph.space);
  NameRooms(numbered, ConnectedSets(graph.space, numbered), first_id, graph);
  AddPortals(sources, graph);
  graph.room_portals = PortalsByRoom(graph);
  return graph;
}

}  // namespace z13::station::rooms
