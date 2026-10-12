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


#include <rooms/free_space.h>

#include <algorithm>
#include <format>
#include <iterator>
#include <limits>
#include <numeric>
#include <unordered_set>

#include <boost/container_hash/hash.hpp>
#include <boost/iterator/function_output_iterator.hpp>

#include <rooms/room_graph.h>

namespace z13::station::rooms {

namespace {

namespace bgi = boost::geometry::index;
using z13::building::primitives::CellBox;
using z13::building::primitives::ChunkOf;
using z13::building::primitives::kCellAxes;

// Free cells kept around the sealed ones: whatever reaches them is open to space.
constexpr int kMargin = 1;

using Binned = std::unordered_map<Eigen::Vector3i, std::vector<CellBox>, z13::building::primitives::CellHash>;
using KeySet = std::unordered_set<Eigen::Vector3i, z13::building::primitives::CellHash>;

LeafBox ToLeafBox(const CellBox& box) {
  return {{box.min.x(), box.min.y(), box.min.z()}, {box.End().x(), box.End().y(), box.End().z()}};
}

bool ContainsBox(const CellBox& outer, const CellBox& inner) {
  return (outer.min.array() <= inner.min.array()).all() && (inner.End().array() <= outer.End().array()).all();
}

// Whether two boxes with disjoint insides share part of a face.
bool ShareFace(const CellBox& a, const CellBox& b) {
  int touching {};
  for (int axis = 0; axis < kCellAxes; ++axis) {
    const int overlap = std::min(a.End()[axis], b.End()[axis]) - std::max(a.min[axis], b.min[axis]);
    if (overlap < 0) {
      return false;
    }
    touching += overlap == 0 ? 1 : 0;
  }
  return touching == 1;
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

struct Split {
  int axis {};
  int at {};
  double cost {};
};

// Where cutting `region` costs least, by the volume on each side times the boxes there:
// cheap cuts run along walls and floors and leave whole free boxes on one side.
Split BestSplit(const CellBox& region, const std::vector<CellBox>& boxes) {
  Split best {.cost = std::numeric_limits<double>::max()};
  for (int axis = 0; axis < kCellAxes; ++axis) {
    std::vector<int> mins;
    std::vector<int> ends;
    std::ranges::transform(boxes, std::back_inserter(mins), [axis](const CellBox& box) { return box.min[axis]; });
    std::ranges::transform(boxes, std::back_inserter(ends), [axis](const CellBox& box) { return box.End()[axis]; });
    std::ranges::sort(mins);
    std::ranges::sort(ends);
    const int low = region.min[axis];
    const int high = region.End()[axis];
    // The area of a cut across this axis, so cuts along different axes compare by volume.
    const double cross = static_cast<double>(region.extent.cast<int64_t>().prod()) / (high - low);
    std::vector<int> candidates;
    std::ranges::merge(mins, ends, std::back_inserter(candidates));
    const auto [first, last] = std::ranges::unique(candidates);
    candidates.erase(first, last);
    for (const int at : candidates) {
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

// The free boxes of `region` left by cutting it along the faces of `boxes`, kd-tree style.
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

std::shared_ptr<const FreeChunk> BuildChunk(const CellBox& region, const std::vector<CellBox>& sealed) {
  FreeChunk chunk;
  CutFree(region, Clipped(sealed, region), chunk.leaves);
  std::vector<LeafEntry> entries;
  for (uint32_t i = 0; i < chunk.leaves.size(); ++i) {
    entries.emplace_back(ToLeafBox(chunk.leaves[i]), i);
  }
  // The range constructor bulk-loads the tree, faster and tighter than inserting one by one.
  chunk.tree = LeafTree(entries);
  return std::make_shared<const FreeChunk>(std::move(chunk));
}

// The sealed boxes' bounds and a free cell around them.
CellBox BoundsOf(const std::vector<CellBox>& sealed) {
  Eigen::Vector3i low = sealed.front().min;
  Eigen::Vector3i high = sealed.front().End();
  for (const CellBox& box : sealed) {
    low = low.cwiseMin(box.min);
    high = high.cwiseMax(box.End());
  }
  low -= Eigen::Vector3i::Constant(kMargin);
  high += Eigen::Vector3i::Constant(kMargin);
  return {.min = low, .extent = high - low};
}

struct ChunkRange {
  Eigen::Vector3i first = Eigen::Vector3i::Zero();
  Eigen::Vector3i last = Eigen::Vector3i::Zero();
};

ChunkRange RangeOf(const CellBox& cells, int chunk_cells) {
  return {.first = ChunkOf(cells.min, chunk_cells), .last = ChunkOf(cells.End() - Eigen::Vector3i::Ones(), chunk_cells)};
}

std::expected<CellBox, std::string> AreaOf(const ChunkRange& range, int chunk_cells) {
  const Eigen::Vector3i counts = range.last - range.first + Eigen::Vector3i::Ones();
  if (static_cast<size_t>(counts.cast<int64_t>().prod()) > kMaxChunks) {
    return std::unexpected(std::format("the station spans {}x{}x{} chunks, more than the {} rooms are built over",
                                       counts.x(), counts.y(), counts.z(), kMaxChunks));
  }
  return CellBox {.min = range.first * chunk_cells, .extent = counts * chunk_cells};
}

// The sealed boxes touching each of `keys`.
Binned BinInto(const std::vector<CellBox>& sealed, const KeySet& keys, int chunk_cells) {
  Binned binned;
  for (const CellBox& box : sealed) {
    const ChunkRange range = RangeOf(box, chunk_cells);
    ForEachChunk(range.first, range.last, [&](const Eigen::Vector3i& chunk) {
      if (keys.contains(chunk)) {
        binned[chunk].push_back(box);
      }
    });
  }
  return binned;
}

std::shared_ptr<const FreeChunk> BuildChunkAt(const Eigen::Vector3i& key, const Binned& binned, int chunk_cells) {
  const auto found = binned.find(key);
  const CellBox region {.min = key * chunk_cells, .extent = Eigen::Vector3i::Constant(chunk_cells)};
  return BuildChunk(region, found == binned.end() ? std::vector<CellBox> {} : found->second);
}

}  // namespace

std::expected<FreeSpace, std::string> FreeSpace::Build(const std::vector<CellBox>& sealed, int chunk_cells) {
  FreeSpace space;
  space.chunk_cells_ = chunk_cells;
  if (sealed.empty()) {
    return space;
  }
  space.bounds_ = BoundsOf(sealed);
  const ChunkRange range = RangeOf(space.bounds_, chunk_cells);
  auto area = AreaOf(range, chunk_cells);
  if (!area) {
    return std::unexpected(std::move(area.error()));
  }
  space.area_ = *area;
  KeySet keys;
  ForEachChunk(range.first, range.last, [&](const Eigen::Vector3i& chunk) { keys.insert(chunk); });
  const Binned binned = BinInto(sealed, keys, chunk_cells);
  for (const Eigen::Vector3i& key : keys) {
    space.chunks_.emplace(key, BuildChunkAt(key, binned, chunk_cells));
  }
  return space;
}

std::expected<RebuiltSpace, std::string> FreeSpace::Rebuilt(const std::vector<CellBox>& sealed,
                                                            const std::vector<CellBox>& changed) const {
  if (sealed.empty() || chunks_.empty()) {
    auto built = Build(sealed, chunk_cells_);
    if (!built) {
      return std::unexpected(std::move(built.error()));
    }
    RebuiltSpace rebuilt {.space = std::move(*built)};
    rebuilt.rebuilt = rebuilt.space.OrderedChunks();
    return rebuilt;
  }
  RebuiltSpace rebuilt {.space = *this};
  FreeSpace& space = rebuilt.space;
  space.bounds_ = BoundsOf(sealed);
  const ChunkRange range = RangeOf(space.bounds_, chunk_cells_);
  auto area = AreaOf(range, chunk_cells_);
  if (!area) {
    return std::unexpected(std::move(area.error()));
  }
  space.area_ = *area;
  // Chunks left outside the new bounds hold nothing sealed and only vacuum: dropped as they are.
  std::erase_if(space.chunks_, [&](const auto& chunk) { return !area->Contains(chunk.first * chunk_cells_); });
  KeySet keys;
  for (const CellBox& box : changed) {
    const ChunkRange touched = RangeOf(box, chunk_cells_);
    ForEachChunk(touched.first, touched.last, [&](const Eigen::Vector3i& chunk) {
      if (area->Contains(chunk * chunk_cells_)) {
        keys.insert(chunk);
      }
    });
  }
  ForEachChunk(range.first, range.last, [&](const Eigen::Vector3i& chunk) {
    if (!chunks_.contains(chunk)) {
      keys.insert(chunk);
    }
  });
  const Binned binned = BinInto(sealed, keys, chunk_cells_);
  for (const Eigen::Vector3i& key : keys) {
    space.chunks_[key] = BuildChunkAt(key, binned, chunk_cells_);
    rebuilt.rebuilt.push_back(key);
  }
  std::ranges::sort(rebuilt.rebuilt, AnchorBefore);
  return rebuilt;
}

std::vector<Eigen::Vector3i> FreeSpace::OrderedChunks() const {
  std::vector<Eigen::Vector3i> keys;
  std::ranges::transform(chunks_, std::back_inserter(keys), [](const auto& chunk) { return chunk.first; });
  std::ranges::sort(keys, AnchorBefore);
  return keys;
}

size_t LeafRefHash::operator()(const LeafRef& ref) const {
  size_t hash = z13::building::primitives::CellHash {}(ref.chunk);
  boost::hash_combine(hash, ref.leaf);
  return hash;
}

size_t FreeSpace::LeafCount() const {
  return std::accumulate(chunks_.begin(), chunks_.end(), size_t {},
                         [](size_t sum, const auto& chunk) { return sum + chunk.second->leaves.size(); });
}

const CellBox& FreeSpace::Leaf(const LeafRef& ref) const {
  return chunks_.at(ref.chunk)->leaves[ref.leaf];
}

std::optional<LeafRef> FreeSpace::LeafAt(const Eigen::Vector3i& cell) const {
  const Eigen::Vector3i key = ChunkOf(cell, chunk_cells_);
  const auto found = chunks_.find(key);
  if (found == chunks_.end()) {
    return std::nullopt;
  }
  const CellPoint point(cell.x(), cell.y(), cell.z());
  // A point on the face two leaves share lies in both closed boxes; the cell is in the
  // one whose half-open box [min, End()) holds it.
  const auto holds = [&point](const LeafEntry& entry) {
    namespace bg = boost::geometry;
    return bg::get<0>(point) < bg::get<bg::max_corner, 0>(entry.first) &&
           bg::get<1>(point) < bg::get<bg::max_corner, 1>(entry.first) &&
           bg::get<2>(point) < bg::get<bg::max_corner, 2>(entry.first);
  };
  std::optional<LeafRef> leaf;
  found->second->tree.query(bgi::intersects(point) && bgi::satisfies(holds),
                            boost::make_function_output_iterator(
                                [&](const LeafEntry& entry) { leaf = LeafRef {.chunk = key, .leaf = entry.second}; }));
  return leaf;
}

std::vector<LeafRef> FreeSpace::LeavesIn(const CellBox& box) const {
  std::vector<LeafRef> found;
  if (chunks_.empty() || !box.Overlaps(area_)) {
    return found;
  }
  const CellBox inside = box.Intersection(area_);
  ForEachChunk(ChunkOf(inside.min, chunk_cells_), ChunkOf(inside.End() - Eigen::Vector3i::Ones(), chunk_cells_),
               [&](const Eigen::Vector3i& key) {
                 const FreeChunk& chunk = *chunks_.at(key);
                 chunk.tree.query(bgi::intersects(ToLeafBox(box)),
                                  boost::make_function_output_iterator([&](const LeafEntry& entry) {
                                    if (chunk.leaves[entry.second].Overlaps(box)) {
                                      found.push_back({.chunk = key, .leaf = entry.second});
                                    }
                                  }));
               });
  return found;
}

std::vector<LeafRef> FreeSpace::Neighbours(const LeafRef& ref) const {
  const CellBox& leaf = Leaf(ref);
  std::vector<LeafRef> found;
  const auto search = [&](const Eigen::Vector3i& key) {
    const auto chunk = chunks_.find(key);
    if (chunk == chunks_.end()) {
      return;
    }
    chunk->second->tree.query(bgi::intersects(ToLeafBox(leaf)),
                              boost::make_function_output_iterator([&](const LeafEntry& entry) {
                                if (ShareFace(leaf, chunk->second->leaves[entry.second])) {
                                  found.push_back({.chunk = key, .leaf = entry.second});
                                }
                              }));
  };
  search(ref.chunk);
  // A leaf shares faces only with leaves of its chunk and of the chunks across the chunk
  // faces it lies on.
  const CellBox chunk_box {.min = ref.chunk * chunk_cells_, .extent = Eigen::Vector3i::Constant(chunk_cells_)};
  for (int axis = 0; axis < kCellAxes; ++axis) {
    const Eigen::Vector3i step = Eigen::Vector3i::Unit(axis);
    if (leaf.min[axis] == chunk_box.min[axis]) {
      search(ref.chunk - step);
    }
    if (leaf.End()[axis] == chunk_box.End()[axis]) {
      search(ref.chunk + step);
    }
  }
  return found;
}

bool FreeSpace::OnOuterFace(const CellBox& leaf) const {
  return (leaf.min.array() == area_.min.array()).any() || (leaf.End().array() == area_.End().array()).any();
}

}  // namespace z13::station::rooms
