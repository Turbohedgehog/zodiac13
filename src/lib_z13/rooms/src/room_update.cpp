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


#include <rooms/room_update.h>

#include <algorithm>
#include <functional>
#include <iterator>
#include <memory>
#include <numeric>
#include <optional>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/connected_components.hpp>

#include <rooms/room_builder.h>

#include "local_leaves.h"
#include "room_parts.h"

namespace z13::station::rooms {

namespace {

using z13::building::primitives::CellBox;
using LeafGraph = boost::adjacency_list<boost::vecS, boost::vecS, boost::undirectedS>;
using KeySet = std::unordered_set<Eigen::Vector3i, z13::building::primitives::CellHash>;


// A new room before it has an id: its extent and the old rooms it took cells from.
struct FoundRoom {
  RoomExtent extent;
  std::unordered_set<RoomId> old_rooms;
  std::optional<RoomId> id;
};

// What an update changes in the sources: the sealed boxes added or removed, and the
// openings of the portal sources added, removed or changed.
struct SourceChanges {
  std::vector<CellBox> sealed;
  bool sealed_added {};
  std::vector<CellBox> openings;
};

SourceChanges ChangesBetween(const RoomSources& before, const RoomSources& after) {
  SourceChanges changes;
  std::ranges::set_symmetric_difference(before.sealed, after.sealed, std::back_inserter(changes.sealed),
                                        SealedBefore);
  changes.sealed_added = std::ranges::any_of(changes.sealed, [&after](const CellBox& box) {
    return std::ranges::binary_search(after.sealed, box, SealedBefore);
  });
  std::vector<PortalSource> portals;
  std::ranges::set_symmetric_difference(before.portals, after.portals, std::back_inserter(portals),
                                        PortalSourceBefore);
  std::ranges::transform(portals, std::back_inserter(changes.openings), &PortalSource::opening);
  return changes;
}

// How many cells the leaf lies from the outside of the area, where space begins.
int DistanceToSpace(const CellBox& leaf, const CellBox& area) {
  const Eigen::Vector3i below = leaf.min - area.min;
  const Eigen::Vector3i above = area.End() - leaf.End();
  return std::min(below.minCoeff(), above.minCoeff());
}

// The leaves of the vacuum that the new blocks closed off from space: each such pocket is
// searched towards the nearest edge of the area, through the old vacuum and the leaves
// found again anyway, and given up on reaching it. Taking in a pocket that is still open is
// harmless, the flood finds it open; only leaves that stayed vacuum are left out of it.
std::vector<LeafRef> ClosedPockets(const RoomGraph& previous, const FreeSpace& space, const LocalLeaves& local,
                                   const KeySet& rebuilt) {
  const auto old_vacuum = [&](const LeafRef& ref) {
    return !rebuilt.contains(ref.chunk) && !local.Contains(ref) && previous.RoomOf(ref) == kVacuumRoom;
  };
  // A pocket touches a rebuilt chunk, though maybe only its new blocks, not its free leaves.
  std::vector<LeafRef> starts;
  for (const Eigen::Vector3i& key : rebuilt) {
    const CellBox chunk {.min = key * space.ChunkCells(), .extent = Eigen::Vector3i::Constant(space.ChunkCells())};
    const CellBox around = chunk.Grown(Eigen::Vector3i::Ones());
    std::ranges::copy_if(space.LeavesIn(around), std::back_inserter(starts), old_vacuum);
  }
  std::unordered_map<LeafRef, bool, LeafRefHash> open_to_space;
  std::vector<LeafRef> pockets;
  for (const LeafRef& start : starts) {
    if (open_to_space.contains(start)) {
      continue;
    }
    using Step = std::pair<int, LeafRef>;
    const auto farther = [](const Step& a, const Step& b) { return a.first > b.first; };
    std::priority_queue<Step, std::vector<Step>, decltype(farther)> pending(farther);
    std::unordered_set<LeafRef, LeafRefHash> seen {start};
    pending.emplace(DistanceToSpace(space.Leaf(start), space.Area()), start);
    bool open = false;
    while (!pending.empty() && !open) {
      const LeafRef leaf = pending.top().second;
      pending.pop();
      const auto known = open_to_space.find(leaf);
      open = space.OnOuterFace(space.Leaf(leaf)) || (known != open_to_space.end() && known->second);
      for (const LeafRef& next : open ? std::vector<LeafRef> {} : space.Neighbours(leaf)) {
        if ((old_vacuum(next) || local.Contains(next)) && seen.insert(next).second) {
          pending.emplace(DistanceToSpace(space.Leaf(next), space.Area()), next);
        }
      }
    }
    for (const LeafRef& leaf : seen) {
      open_to_space[leaf] = open;
      if (!open && old_vacuum(leaf)) {
        pockets.push_back(leaf);
      }
    }
  }
  return pockets;
}

// Per local leaf, its connected set; leaves open to space, or next to the old vacuum left
// out of the flood, join the vertex after the last leaf.
std::vector<size_t> ConnectedSets(const FreeSpace& space, const LocalLeaves& local) {
  const size_t vacuum = local.Leaves().size();
  LeafGraph graph(vacuum + 1);
  for (size_t i = 0; i < local.Leaves().size(); ++i) {
    if (space.OnOuterFace(space.Leaf(local.Leaves()[i]))) {
      boost::add_edge(i, vacuum, graph);
    }
    for (const LeafRef& neighbour : space.Neighbours(local.Leaves()[i])) {
      const auto found = local.IndexOf(neighbour);
      if (!found) {
        boost::add_edge(i, vacuum, graph);
      } else if (*found > i) {
        boost::add_edge(i, *found, graph);
      }
    }
  }
  std::vector<size_t> set_of(vacuum + 1);
  boost::connected_components(graph, set_of.data());
  return set_of;
}

// The old rooms a new leaf took cells from: its own room if its chunk was kept, else the
// rooms of the old leaves it overlaps.
std::unordered_set<RoomId> OldRoomsOf(const RoomGraph& previous, const KeySet& rebuilt, const LeafRef& ref,
                                      const CellBox& leaf) {
  std::unordered_set<RoomId> rooms;
  if (!rebuilt.contains(ref.chunk)) {
    rooms.insert(previous.RoomOf(ref));
  } else {
    for (const LeafRef& old : previous.space.LeavesIn(leaf)) {
      rooms.insert(previous.RoomOf(old));
    }
  }
  rooms.erase(kVacuumRoom);
  return rooms;
}

// Gives the new rooms ids: each old room, by anchor, to the new room holding its old anchor,
// or failing that (its anchor was built over) to the first by anchor of those it overlaps;
// the rest new ids from `next_id` on, by anchor.
void NameRooms(const RoomGraph& previous, const std::vector<RoomId>& old_rooms,
               const std::function<std::optional<size_t>(const Eigen::Vector3i&)>& found_at,
               std::vector<FoundRoom>& found, RoomId& next_id) {
  std::vector<size_t> by_anchor(found.size());
  std::iota(by_anchor.begin(), by_anchor.end(), size_t {});
  std::ranges::sort(by_anchor, [&found](size_t a, size_t b) {
    return AnchorBefore(found[a].extent.anchor, found[b].extent.anchor);
  });
  for (const RoomId old : old_rooms) {
    std::optional<size_t> heir = found_at(previous.rooms.at(old).anchor);
    if (!heir || found[*heir].id || !found[*heir].old_rooms.contains(old)) {
      const auto first = std::ranges::find_if(
          by_anchor, [&](size_t room) { return !found[room].id && found[room].old_rooms.contains(old); });
      heir = first == by_anchor.end() ? std::nullopt : std::optional(*first);
    }
    if (heir) {
      found[*heir].id = old;
    }
  }
  for (const size_t room : by_anchor) {
    if (!found[room].id) {
      found[room].id = next_id++;
    }
  }
}

// The opening and the cells on both sides of it, which its portals depend on.
CellBox PortalReach(const CellBox& opening, Axis axis) {
  return opening.Grown(Eigen::Vector3i::Unit(static_cast<Eigen::Index>(axis)));
}

// `sorted` less `removed`, plus `added`, all three sorted by `before`; nullopt when
// `removed` holds one `sorted` lacks.
template <typename T, typename Before>
std::optional<std::vector<T>> Edited(const std::vector<T>& sorted, const std::vector<T>& added,
                                     const std::vector<T>& removed, Before before) {
  std::vector<T> kept;
  std::ranges::set_difference(sorted, removed, std::back_inserter(kept), before);
  if (kept.size() + removed.size() != sorted.size()) {
    return std::nullopt;
  }
  std::vector<T> edited;
  edited.reserve(kept.size() + added.size());
  std::ranges::merge(kept, added, std::back_inserter(edited), before);
  return edited;
}

// `sorted` as UpdateRooms takes it, sorted, with what changed since `previous.sources`.
std::expected<RoomGraph, std::string> UpdateSorted(const RoomGraph& previous, RoomSources sorted,
                                                   const SourceChanges& changes, RoomId first_id, int chunk_cells) {
  if (previous.space.AllChunks().empty() || sorted.sealed.empty() || previous.space.ChunkCells() != chunk_cells) {
    return BuildRooms(sorted, first_id, chunk_cells);
  }
  auto rebuilt_space = previous.space.Rebuilt(sorted.sealed, changes.sealed);
  if (!rebuilt_space) {
    return std::unexpected(std::move(rebuilt_space.error()));
  }
  RoomGraph graph {.rooms = previous.rooms,
                   .space = std::move(rebuilt_space->space),
                   .leaf_rooms = previous.leaf_rooms,
                   .next_id = first_id,
                   .sources = std::move(sorted)};
  std::erase_if(graph.leaf_rooms, [&graph](const auto& entry) { return !graph.space.AllChunks().contains(entry.first); });
  const KeySet rebuilt(rebuilt_space->rebuilt.begin(), rebuilt_space->rebuilt.end());

  // The leaves whose rooms may change: those of the rebuilt chunks, and every leaf of the
  // old rooms there or next to them. The vacuum's own leaves stay out: whatever reaches
  // them is still open to space.
  LocalLeaves local;
  std::unordered_set<RoomId> touched;
  for (const Eigen::Vector3i& key : rebuilt_space->rebuilt) {
    if (const auto old = previous.leaf_rooms.find(key); old != previous.leaf_rooms.end()) {
      touched.insert(old->second->begin(), old->second->end());
    }
    const auto count = static_cast<uint32_t>(graph.space.AllChunks().at(key)->leaves.size());
    for (uint32_t leaf = 0; leaf < count; ++leaf) {
      local.Add({.chunk = key, .leaf = leaf});
      for (const LeafRef& neighbour : graph.space.Neighbours({.chunk = key, .leaf = leaf})) {
        if (!rebuilt.contains(neighbour.chunk)) {
          touched.insert(previous.RoomOf(neighbour));
        }
      }
    }
  }
  std::vector<RoomId> old_rooms;
  std::ranges::copy_if(touched, std::back_inserter(old_rooms), [](RoomId room) { return room != kVacuumRoom; });
  std::ranges::sort(old_rooms, [&previous](RoomId a, RoomId b) {
    return AnchorBefore(previous.rooms.at(a).anchor, previous.rooms.at(b).anchor);
  });
  for (const RoomId room : old_rooms) {
    for (const LeafRef& ref : previous.space.LeavesIn(previous.rooms.at(room).bounds)) {
      if (!rebuilt.contains(ref.chunk) && previous.RoomOf(ref) == room) {
        local.Add(ref);
      }
    }
  }
  if (touched.contains(kVacuumRoom) && changes.sealed_added) {
    std::ranges::for_each(ClosedPockets(previous, graph.space, local, rebuilt),
                          [&local](const LeafRef& ref) { local.Add(ref); });
  }

  const std::vector<size_t> set_of = ConnectedSets(graph.space, local);
  const size_t vacuum_set = set_of.back();
  std::unordered_map<size_t, size_t> found_of_set;
  std::vector<FoundRoom> found;
  std::vector<std::optional<size_t>> found_of_leaf(local.Leaves().size());
  for (size_t i = 0; i < local.Leaves().size(); ++i) {
    if (set_of[i] == vacuum_set) {
      continue;
    }
    const CellBox& leaf = graph.space.Leaf(local.Leaves()[i]);
    const auto [entry, added] = found_of_set.try_emplace(set_of[i], found.size());
    if (added) {
      found.push_back({.extent = ExtentOf(leaf)});
    } else {
      Absorb(found[entry->second].extent, leaf);
    }
    found[entry->second].old_rooms.merge(OldRoomsOf(previous, rebuilt, local.Leaves()[i], leaf));
    found_of_leaf[i] = entry->second;
  }
  const auto found_at = [&](const Eigen::Vector3i& cell) -> std::optional<size_t> {
    const auto leaf = graph.space.LeafAt(cell);
    const auto index = leaf ? local.IndexOf(*leaf) : std::nullopt;
    return index ? found_of_leaf[*index] : std::nullopt;
  };
  RoomId next_id = first_id;
  NameRooms(previous, old_rooms, found_at, found, next_id);
  graph.next_id = next_id;

  std::vector<CellBox> changed_bounds;
  for (const RoomId room : old_rooms) {
    changed_bounds.push_back(graph.rooms.at(room).bounds);
    graph.rooms.erase(room);
  }
  for (const FoundRoom& room : found) {
    graph.rooms.emplace(*room.id, ToRoom(room.extent));
    changed_bounds.push_back(graph.rooms.at(*room.id).bounds);
  }
  graph.rooms.at(kVacuumRoom).bounds = graph.space.Bounds();

  // Every rebuilt chunk gets new labels, even one left without leaves: the old ones name
  // rooms that may be gone.
  std::unordered_map<Eigen::Vector3i, std::vector<RoomId>, z13::building::primitives::CellHash> relabelled;
  for (const Eigen::Vector3i& key : rebuilt_space->rebuilt) {
    relabelled.emplace(key, std::vector<RoomId>(graph.space.AllChunks().at(key)->leaves.size()));
  }
  for (size_t i = 0; i < local.Leaves().size(); ++i) {
    const LeafRef& ref = local.Leaves()[i];
    auto [labels, added] = relabelled.try_emplace(ref.chunk);
    if (added) {
      labels->second = *previous.leaf_rooms.at(ref.chunk);
    }
    labels->second[ref.leaf] = found_of_leaf[i] ? *found[*found_of_leaf[i]].id : kVacuumRoom;
  }
  for (auto& [key, labels] : relabelled) {
    graph.leaf_rooms[key] = std::make_shared<const std::vector<RoomId>>(std::move(labels));
  }

  for (const Eigen::Vector3i& key : rebuilt_space->rebuilt) {
    changed_bounds.push_back(
        {.min = key * graph.space.ChunkCells(), .extent = Eigen::Vector3i::Constant(graph.space.ChunkCells())});
  }
  std::ranges::copy(changes.openings, std::back_inserter(changed_bounds));
  const auto dirty = [&changed_bounds](const CellBox& reach) {
    return std::ranges::any_of(changed_bounds, [&reach](const CellBox& box) { return box.Overlaps(reach); });
  };
  graph.portals.clear();
  std::ranges::copy_if(previous.portals, std::back_inserter(graph.portals),
                       [&](const Portal& portal) { return !dirty(PortalReach(portal.opening, portal.axis)); });
  for (const PortalSource& source : graph.sources.portals) {
    if (dirty(PortalReach(source.opening, source.axis))) {
      std::ranges::copy(PortalsThrough(graph, source), std::back_inserter(graph.portals));
    }
  }
  std::ranges::sort(graph.portals, PortalBefore);
  graph.room_portals = PortalsByRoom(graph.portals);
  return graph;
}

}  // namespace

std::expected<RoomGraph, std::string> UpdateRooms(const RoomGraph& previous, const RoomSources& sources,
                                                  RoomId first_id, int chunk_cells) {
  RoomSources sorted = sources;
  std::ranges::sort(sorted.sealed, SealedBefore);
  std::ranges::sort(sorted.portals, PortalSourceBefore);
  const SourceChanges changes = ChangesBetween(previous.sources, sorted);
  return UpdateSorted(previous, std::move(sorted), changes, first_id, chunk_cells);
}

std::expected<RoomGraph, std::string> UpdateRooms(const RoomGraph& previous, const RoomSourceEdits& edits,
                                                  RoomId first_id, int chunk_cells) {
  RoomSourceEdits sorted = edits;
  for (RoomSources* sources : {&sorted.added, &sorted.removed}) {
    std::ranges::sort(sources->sealed, SealedBefore);
    std::ranges::sort(sources->portals, PortalSourceBefore);
  }
  auto sealed = Edited(previous.sources.sealed, sorted.added.sealed, sorted.removed.sealed, SealedBefore);
  auto portals = Edited(previous.sources.portals, sorted.added.portals, sorted.removed.portals, PortalSourceBefore);
  if (!sealed || !portals) {
    return std::unexpected(std::string {"the edits remove a room source the graph lacks"});
  }
  return UpdateSorted(previous, {.sealed = std::move(*sealed), .portals = std::move(*portals)},
                      ChangesBetween(sorted.removed, sorted.added), first_id, chunk_cells);
}

}  // namespace z13::station::rooms
