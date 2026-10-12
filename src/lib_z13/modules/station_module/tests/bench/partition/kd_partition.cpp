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

#include "kd_partition.h"

#include <algorithm>
#include <iterator>
#include <limits>
#include <numeric>

#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/connected_components.hpp>
#include <boost/iterator/function_output_iterator.hpp>

#include <rooms/room_graph.h>

namespace z13::testing::partition {

namespace {

namespace bgi = boost::geometry::index;
using z13::building::primitives::CellBox;
using LeafGraph = boost::adjacency_list<boost::vecS, boost::vecS, boost::undirectedS>;

constexpr int kAxes = 3;
// Free cells kept around the sealed ones, as the column map keeps them.
constexpr int kMargin = 1;

int64_t Volume(const CellBox& box) {
  return static_cast<int64_t>(box.extent.x()) * box.extent.y() * box.extent.z();
}

bool ContainsBox(const CellBox& outer, const CellBox& inner) {
  return (outer.min.array() <= inner.min.array()).all() && (inner.End().array() <= outer.End().array()).all();
}

LeafBox ToLeafBox(const CellBox& box) {
  return {{box.min.x(), box.min.y(), box.min.z()}, {box.End().x(), box.End().y(), box.End().z()}};
}

LeafBox CellQuery(const Eigen::Vector3i& cell) {
  return {{cell.x(), cell.y(), cell.z()}, {cell.x() + 1, cell.y() + 1, cell.z() + 1}};
}

// Whether two boxes with disjoint insides share part of a face, not just an edge or a corner.
bool ShareFace(const CellBox& a, const CellBox& b) {
  int touching {};
  for (int axis = 0; axis < kAxes; ++axis) {
    const int overlap = std::min(a.End()[axis], b.End()[axis]) - std::max(a.min[axis], b.min[axis]);
    if (overlap < 0) {
      return false;
    }
    touching += overlap == 0 ? 1 : 0;
  }
  return touching == 1;
}

struct Split {
  int axis {};
  int at {};
  double cost {};
};

// Where splitting `region` costs least, by the volume of each side times the boxes on it:
// cheap splits run along walls and floors and leave whole free boxes on one side.
Split BestSplit(const CellBox& region, const std::vector<CellBox>& boxes) {
  Split best {.cost = std::numeric_limits<double>::max()};
  for (int axis = 0; axis < kAxes; ++axis) {
    std::vector<int> mins;
    std::vector<int> ends;
    for (const CellBox& box : boxes) {
      mins.push_back(box.min[axis]);
      ends.push_back(box.End()[axis]);
    }
    std::ranges::sort(mins);
    std::ranges::sort(ends);
    const int low = region.min[axis];
    const int high = region.End()[axis];
    // The area of a cut across this axis, so cuts along different axes compare by volume.
    const double cross = static_cast<double>(Volume(region)) / (high - low);
    for (const std::vector<int>* faces : {&mins, &ends}) {
      for (const int at : *faces) {
        if (at <= low || at >= high) {
          continue;
        }
        const auto on_low = std::ranges::lower_bound(mins, at) - mins.begin();
        const auto on_high = ends.end() - std::ranges::upper_bound(ends, at);
        const double cost = cross * (static_cast<double>(at - low) * static_cast<double>(on_low) +
                                     static_cast<double>(high - at) * static_cast<double>(on_high));
        if (cost < best.cost) {
          best = {.axis = axis, .at = at, .cost = cost};
        }
      }
    }
  }
  return best;
}

std::vector<CellBox> Clipped(const std::vector<CellBox>& boxes, const CellBox& region) {
  std::vector<CellBox> clipped;
  for (const CellBox& box : boxes) {
    if (box.Overlaps(region)) {
      clipped.push_back(box.Intersection(region));
    }
  }
  return clipped;
}

// The free boxes of `region` left by cutting it along the faces of `boxes`.
void CutFree(const CellBox& region, const std::vector<CellBox>& boxes, std::vector<CellBox>& leaves) {
  if (boxes.empty()) {
    leaves.push_back(region);
    return;
  }
  if (std::ranges::any_of(boxes, [&region](const CellBox& box) { return ContainsBox(box, region); })) {
    return;
  }
  const Split split = BestSplit(region, boxes);
  CellBox low = region;
  low.extent[split.axis] = split.at - region.min[split.axis];
  CellBox high = region;
  high.min[split.axis] = split.at;
  high.extent[split.axis] = region.End()[split.axis] - split.at;
  CutFree(low, Clipped(boxes, low), leaves);
  CutFree(high, Clipped(boxes, high), leaves);
}

template <typename Visit>
void ForEachChunk(const Eigen::Vector3i& first, const Eigen::Vector3i& last, Visit visit) {
  for (int z = first.z(); z <= last.z(); ++z) {
    for (int y = first.y(); y <= last.y(); ++y) {
      for (int x = first.x(); x <= last.x(); ++x) {
        visit(Eigen::Vector3i(x, y, z));
      }
    }
  }
}

}  // namespace

KdPartition::KdPartition(const std::vector<CellBox>& sealed, int chunk_cells) : chunk_cells_(chunk_cells) {
  Eigen::Vector3i low = sealed.front().min;
  Eigen::Vector3i high = sealed.front().End();
  for (const CellBox& box : sealed) {
    low = low.cwiseMin(box.min);
    high = high.cwiseMax(box.End());
  }
  low_chunk_ = z13::building::primitives::ChunkOf(low - Eigen::Vector3i::Constant(kMargin), chunk_cells_);
  high_chunk_ = z13::building::primitives::ChunkOf(high + Eigen::Vector3i::Constant(kMargin - 1), chunk_cells_) +
                Eigen::Vector3i::Ones();
  const Binned binned = Bin(sealed);
  ForEachChunk(low_chunk_, high_chunk_ - Eigen::Vector3i::Ones(), [&](const Eigen::Vector3i& chunk) {
    const auto found = binned.find(chunk);
    BuildChunk(chunk, found == binned.end() ? std::vector<CellBox> {} : found->second);
  });
}

KdPartition::Binned KdPartition::Bin(const std::vector<CellBox>& sealed) const {
  Binned binned;
  for (const CellBox& box : sealed) {
    ForEachChunk(z13::building::primitives::ChunkOf(box.min, chunk_cells_),
                 z13::building::primitives::ChunkOf(box.End() - Eigen::Vector3i::Ones(), chunk_cells_),
                 [&](const Eigen::Vector3i& chunk) { binned[chunk].push_back(box); });
  }
  return binned;
}

void KdPartition::Rebuild(const std::vector<CellBox>& sealed, const CellBox& box) {
  const Eigen::Vector3i first = z13::building::primitives::ChunkOf(box.min, chunk_cells_);
  const Eigen::Vector3i last = z13::building::primitives::ChunkOf(box.End() - Eigen::Vector3i::Ones(), chunk_cells_);
  const CellBox touched {.min = first * chunk_cells_, .extent = (last - first + Eigen::Vector3i::Ones()) * chunk_cells_};
  const Binned binned = Bin(Clipped(sealed, touched));
  ForEachChunk(first, last, [&](const Eigen::Vector3i& chunk) {
    const auto found = binned.find(chunk);
    BuildChunk(chunk, found == binned.end() ? std::vector<CellBox> {} : found->second);
  });
}

CellBox KdPartition::ChunkBox(const Eigen::Vector3i& chunk) const {
  return {.min = chunk * chunk_cells_, .extent = Eigen::Vector3i::Constant(chunk_cells_)};
}

void KdPartition::BuildChunk(const Eigen::Vector3i& chunk, const std::vector<CellBox>& sealed) {
  const CellBox region = ChunkBox(chunk);
  KdChunk built;
  CutFree(region, Clipped(sealed, region), built.leaves);
  std::vector<LeafEntry> entries;
  for (uint32_t i = 0; i < built.leaves.size(); ++i) {
    entries.emplace_back(ToLeafBox(built.leaves[i]), i);
  }
  // The packing constructor bulk-loads the tree, faster and tighter than inserting one by one.
  built.tree = LeafTree(entries);
  chunks_[chunk] = std::move(built);
}

size_t KdPartition::LeafCount() const {
  return std::accumulate(chunks_.begin(), chunks_.end(), size_t {},
                         [](size_t sum, const auto& chunk) { return sum + chunk.second.leaves.size(); });
}

size_t KdPartition::Bytes() const {
  return std::accumulate(chunks_.begin(), chunks_.end(), size_t {}, [](size_t sum, const auto& chunk) {
    return sum + chunk.second.leaves.capacity() * sizeof(CellBox) + chunk.second.tree.get_allocator().Bytes();
  });
}

std::optional<CellBox> KdPartition::LeafAt(const Eigen::Vector3i& cell) const {
  const auto found = chunks_.find(z13::building::primitives::ChunkOf(cell, chunk_cells_));
  if (found == chunks_.end()) {
    return std::nullopt;
  }
  // Leaves have disjoint insides, so only the leaf holding the cell contains its box.
  std::optional<CellBox> leaf;
  found->second.tree.query(bgi::contains(CellQuery(cell)),
                           boost::make_function_output_iterator([&](const LeafEntry& entry) {
                             leaf = found->second.leaves[entry.second];
                           }));
  return leaf;
}

std::vector<CellBox> KdPartition::LeavesIn(const CellBox& box) const {
  std::vector<CellBox> found;
  ForEachChunk(z13::building::primitives::ChunkOf(box.min, chunk_cells_),
               z13::building::primitives::ChunkOf(box.End() - Eigen::Vector3i::Ones(), chunk_cells_),
               [&](const Eigen::Vector3i& chunk_key) {
                 const auto chunk = chunks_.find(chunk_key);
                 if (chunk == chunks_.end()) {
                   return;
                 }
                 chunk->second.tree.query(bgi::intersects(ToLeafBox(box)),
                                          boost::make_function_output_iterator([&](const LeafEntry& entry) {
                                            const CellBox& leaf = chunk->second.leaves[entry.second];
                                            if (leaf.Overlaps(box)) {
                                              found.push_back(leaf);
                                            }
                                          }));
               });
  return found;
}

KdRooms KdPartition::FloodRooms() const {
  std::vector<Eigen::Vector3i> keys;
  std::ranges::transform(chunks_, std::back_inserter(keys), [](const auto& chunk) { return chunk.first; });
  std::ranges::sort(keys, z13::station::rooms::AnchorBefore);
  std::unordered_map<Eigen::Vector3i, uint32_t, z13::building::primitives::CellHash> first_leaf;
  std::vector<CellBox> leaves;
  for (const Eigen::Vector3i& key : keys) {
    first_leaf[key] = static_cast<uint32_t>(leaves.size());
    const auto& chunk_leaves = chunks_.at(key).leaves;
    leaves.insert(leaves.end(), chunk_leaves.begin(), chunk_leaves.end());
  }
  const auto vacuum = static_cast<uint32_t>(leaves.size());
  LeafGraph graph(leaves.size() + 1);
  const Eigen::Vector3i outer_low = low_chunk_ * chunk_cells_;
  const Eigen::Vector3i outer_high = high_chunk_ * chunk_cells_;
  for (const Eigen::Vector3i& key : keys) {
    const uint32_t offset = first_leaf.at(key);
    const auto& chunk_leaves = chunks_.at(key).leaves;
    for (uint32_t i = 0; i < chunk_leaves.size(); ++i) {
      const CellBox& leaf = chunk_leaves[i];
      if ((leaf.min.array() == outer_low.array()).any() || (leaf.End().array() == outer_high.array()).any()) {
        boost::add_edge(offset + i, vacuum, graph);
      }
      // Neighbours in this chunk and, across its faces, in the chunks after it.
      ForEachChunk(key, z13::building::primitives::ChunkOf(leaf.End(), chunk_cells_),
                   [&](const Eigen::Vector3i& other_key) {
                     const auto other = chunks_.find(other_key);
                     if (other == chunks_.end()) {
                       return;
                     }
                     const uint32_t other_offset = first_leaf.at(other_key);
                     other->second.tree.query(
                         bgi::intersects(ToLeafBox(leaf)),
                         boost::make_function_output_iterator([&](const LeafEntry& entry) {
                           const uint32_t j = other_offset + entry.second;
                           if (j > offset + i && ShareFace(leaf, other->second.leaves[entry.second])) {
                             boost::add_edge(offset + i, j, graph);
                           }
                         }));
                   });
    }
  }
  std::vector<int> component(boost::num_vertices(graph));
  boost::connected_components(graph, component.data());
  std::unordered_map<int, size_t> room_of_component;
  KdRooms rooms;
  for (uint32_t i = 0; i < leaves.size(); ++i) {
    if (component[i] == component[vacuum]) {
      continue;
    }
    const auto [found, added] = room_of_component.try_emplace(component[i], rooms.anchors.size());
    if (added) {
      rooms.anchors.push_back(leaves[i].min);
      rooms.volumes.push_back(0);
    }
    Eigen::Vector3i& anchor = rooms.anchors[found->second];
    if (z13::station::rooms::AnchorBefore(leaves[i].min, anchor)) {
      anchor = leaves[i].min;
    }
    rooms.volumes[found->second] += Volume(leaves[i]);
  }
  return rooms;
}

}  // namespace z13::testing::partition
