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
#include <iterator>
#include <cstddef>
#include <memory>
#include <numeric>
#include <unordered_map>
#include <utility>

#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/connected_components.hpp>

#include <lib_profile/profile.h>
#include <rooms/openings.h>

#include "numbered_leaves.h"
#include "room_parts.h"

namespace z13::station::rooms {

namespace {

using z13::building::primitives::CellBox;
using LeafGraph = boost::adjacency_list<boost::vecS, boost::vecS, boost::undirectedS>;

// Per leaf, its connected set; the leaves open to space join the vertex after the last leaf.
std::vector<size_t> ConnectedSets(const FreeSpace& space, const NumberedLeaves& numbered) {
  Z13_PROFILE_ZONE("ConnectedSets");
  const size_t vacuum = numbered.Leaves().size();
  LeafGraph graph(vacuum + 1);
  for (size_t i = 0; i < numbered.Leaves().size(); ++i) {
    if (space.OnOuterFace(space.Leaf(numbered.Leaves()[i]))) {
      boost::add_edge(i, vacuum, graph);
    }
    for (const LeafRef& neighbour : space.Neighbours(numbered.Leaves()[i])) {
      if (const size_t j = numbered.Number(neighbour); j > i) {
        boost::add_edge(i, j, graph);
      }
    }
  }
  std::vector<size_t> set_of(vacuum + 1);
  boost::connected_components(graph, set_of.data());
  return set_of;
}

// Names the connected sets: the vacuum's set is room 0, the rest get ids by anchor from
// `first_id`. Fills the graph's rooms and the room of every leaf.
void NameRooms(const NumberedLeaves& numbered, const std::vector<size_t>& set_of, RoomId first_id, RoomGraph& graph) {
  const size_t vacuum_set = set_of.back();
  std::unordered_map<size_t, RoomExtent> extents;
  for (size_t i = 0; i < numbered.Leaves().size(); ++i) {
    if (set_of[i] == vacuum_set) {
      continue;
    }
    const CellBox& leaf = graph.space.Leaf(numbered.Leaves()[i]);
    const auto [extent, added] = extents.try_emplace(set_of[i], ExtentOf(leaf));
    if (!added) {
      Absorb(extent->second, leaf);
    }
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
    const size_t first = numbered.FirstOf(key);
    for (size_t leaf = 0; leaf < rooms.size(); ++leaf) {
      rooms[leaf] = id_of_set.at(set_of[first + leaf]);
    }
    graph.leaf_rooms.emplace(key, std::make_shared<const std::vector<RoomId>>(std::move(rooms)));
  }
}

}  // namespace

std::expected<RoomGraph, std::string> BuildRooms(const RoomSources& sources, RoomId first_id,
                                                 const RoomSettings& settings) {
  Z13_PROFILE_ZONE("BuildRooms");
  std::vector<PortalSource> openings = FindOpenings(sources.sealed, sources.sealed, settings.max_opening_cells);
  auto space = FreeSpace::Build(SealedWith(sources.sealed, openings), settings.chunk_cells);
  if (!space) {
    return std::unexpected(std::move(space.error()));
  }
  RoomGraph graph {.space = std::move(*space), .next_id = first_id, .settings = settings};
  const NumberedLeaves numbered(graph.space);
  NameRooms(numbered, ConnectedSets(graph.space, numbered), first_id, graph);
  std::vector<PortalSource> ordered = sources.portals;
  std::ranges::copy(openings, std::back_inserter(ordered));
  std::ranges::sort(ordered, [](const PortalSource& a, const PortalSource& b) {
    return AnchorBefore(a.opening.min, b.opening.min);
  });
  for (const PortalSource& source : ordered) {
    std::ranges::copy(PortalsThrough(graph, source), std::back_inserter(graph.portals));
  }
  graph.room_portals = PortalsByRoom(graph.portals);
  graph.sources = sources;
  std::ranges::sort(graph.sources.sealed, SealedBefore);
  std::ranges::sort(graph.sources.portals, PortalSourceBefore);
  graph.openings = std::move(openings);
  return graph;
}

}  // namespace z13::station::rooms
