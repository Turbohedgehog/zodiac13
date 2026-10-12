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

// The free space of the shipped stations and of a scene with oblique walls as the column
// map against a kd-tree per chunk (docs/oblique-walls-plan.md, f/partition-bench). Built by
// `make.py --bench`, run with `z13.py --bench --filter 'PartitionBench.*'`.

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <format>
#include <functional>
#include <optional>
#include <iostream>
#include <map>
#include <random>
#include <string>
#include <tuple>
#include <vector>

#include <boost/iterator/function_output_iterator.hpp>

#include <primitives/station_assets.h>
#include <rooms/portal_source.h>
#include <rooms/room_builder.h>
#include <z13_tests/shipped_station.h>

#include "partition/kd_partition.h"

namespace z13::testing {
namespace {

namespace primitives = z13::building::primitives;
using primitives::CellBox;
using z13::station::rooms::RoomGraph;
using z13::station::rooms::RoomSources;

constexpr int kChunkSizes[] = {32, 64};
constexpr int kQueries = 200'000;
constexpr int kRebuiltChunks = 20;
constexpr uint32_t kSeed = 13;
constexpr double kBytesPerMb = 1024.0 * 1024.0;

template <typename F>
double Ms(F&& work) {
  const auto start = std::chrono::steady_clock::now();
  work();
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

RoomSources ShippedSources(const std::string& scene, const primitives::Palette& palette) {
  RoomSources sources;
  const auto blocks = ShippedBlueprint(scene, palette);
  EXPECT_TRUE(blocks.has_value()) << blocks.error();
  for (const z13::station::Block& block : blocks.value_or(std::vector<z13::station::Block> {})) {
    const primitives::Primitive& primitive = palette.Find(block.spec.type_id)->get();
    if (!z13::station::rooms::IsSealing(primitive)) {
      continue;
    }
    sources.sealed.push_back(primitives::OccupiedCells(block));
    if (const auto portal = z13::station::rooms::PortalOf(block, primitive)) {
      sources.portals.push_back(*portal);
    }
  }
  return sources;
}

// The cells of a wall one cell thick from `from` to `to` in the xy plane, `height` tall,
// rasterized so that no diagonal gap lets air through: each row along y is one box.
void AddObliqueWall(std::vector<CellBox>& boxes, const Eigen::Vector2i& from, const Eigen::Vector2i& to, int floor,
                    int height) {
  const Eigen::Vector2i delta = to - from;
  const int rows = std::abs(delta.y());
  const int step = delta.y() >= 0 ? 1 : -1;
  for (int row = 0; row <= rows; ++row) {
    const double t0 = rows == 0 ? 0.0 : static_cast<double>(row) / (rows + 1);
    const double t1 = rows == 0 ? 1.0 : static_cast<double>(row + 1) / (rows + 1);
    const int x0 = from.x() + static_cast<int>(std::floor(delta.x() * t0));
    const int x1 = from.x() + static_cast<int>(std::floor(delta.x() * t1));
    const int low = std::min(x0, x1);
    const int high = std::max(x0, x1) + 1;
    boxes.push_back({.min = {low, from.y() + step * row, floor}, .extent = {high - low, 1, height}});
  }
}

// A hall with a 1:1 and a 1:2 wall across it, an octagonal room and a roof sloping 1:2.
std::vector<CellBox> ObliqueScene() {
  constexpr int kSide = 200;
  constexpr int kHeight = 12;
  // Tall enough for the roof's ridge.
  constexpr int kOuterHeight = kHeight + kSide / 2 + 2;
  std::vector<CellBox> boxes {
      {.min = {0, 0, 0}, .extent = {kSide, kSide, 1}},
      {.min = {0, 0, 0}, .extent = {1, kSide, kOuterHeight}},
      {.min = {kSide - 1, 0, 0}, .extent = {1, kSide, kOuterHeight}},
      {.min = {1, 0, 0}, .extent = {kSide - 2, 1, kOuterHeight}},
      {.min = {1, kSide - 1, 0}, .extent = {kSide - 2, 1, kOuterHeight}},
  };
  AddObliqueWall(boxes, {10, 10}, {90, 90}, 1, kHeight);
  AddObliqueWall(boxes, {100, 10}, {140, 90}, 1, kHeight);
  // An octagon of side 24 around (150, 150): four axis walls and four diagonal ones.
  constexpr int kO = 132;
  constexpr int kS = 12;
  boxes.push_back({.min = {kO + kS, kO, 1}, .extent = {kS, 1, kHeight}});
  boxes.push_back({.min = {kO + kS, kO + 3 * kS, 1}, .extent = {kS, 1, kHeight}});
  boxes.push_back({.min = {kO, kO + kS, 1}, .extent = {1, kS, kHeight}});
  boxes.push_back({.min = {kO + 3 * kS, kO + kS, 1}, .extent = {1, kS, kHeight}});
  AddObliqueWall(boxes, {kO + kS, kO}, {kO, kO + kS}, 1, kHeight);
  AddObliqueWall(boxes, {kO + 2 * kS, kO}, {kO + 3 * kS, kO + kS}, 1, kHeight);
  AddObliqueWall(boxes, {kO, kO + 2 * kS}, {kO + kS, kO + 3 * kS}, 1, kHeight);
  AddObliqueWall(boxes, {kO + 3 * kS, kO + 2 * kS}, {kO + 2 * kS, kO + 3 * kS}, 1, kHeight);
  // A roof rising one cell every two along x, as a staircase of slabs.
  for (int x = 1; x < kSide - 1; x += 2) {
    boxes.push_back({.min = {x, 1, kHeight + 1 + x / 2}, .extent = {2, kSide - 2, 1}});
  }
  return boxes;
}

using RoomKey = std::tuple<int, int, int>;

std::map<RoomKey, int64_t> ColumnRooms(const RoomGraph& graph) {
  std::map<RoomKey, int64_t> rooms;
  for (const auto id : graph.ByAnchor()) {
    const auto& room = graph.rooms.at(id);
    rooms[{room.anchor.x(), room.anchor.y(), room.anchor.z()}] = room.volume_cells;
  }
  return rooms;
}

std::map<RoomKey, int64_t> KdRoomsByAnchor(const partition::KdRooms& kd) {
  std::map<RoomKey, int64_t> rooms;
  for (size_t i = 0; i < kd.anchors.size(); ++i) {
    rooms[{kd.anchors[i].x(), kd.anchors[i].y(), kd.anchors[i].z()}] = kd.volumes[i];
  }
  return rooms;
}

std::vector<Eigen::Vector3i> RandomCells(const RoomGraph& graph) {
  std::mt19937 random(kSeed);
  const auto& columns = graph.columns;
  std::uniform_int_distribution<int> x(columns.Origin().x(), columns.Origin().x() + columns.Size().x() - 1);
  std::uniform_int_distribution<int> y(columns.Origin().y(), columns.Origin().y() + columns.Size().y() - 1);
  std::uniform_int_distribution<int> z(columns.Heights().x(), columns.Heights().y() - 1);
  std::vector<Eigen::Vector3i> cells;
  for (int i = 0; i < kQueries; ++i) {
    cells.emplace_back(x(random), y(random), z(random));
  }
  return cells;
}

void Measure(const std::string& name, const RoomSources& sources) {
  RoomGraph graph;
  const double columns_ms = Ms([&] { graph = z13::station::rooms::BuildRooms(sources).value(); });
  const size_t columns_bytes = graph.columns.Intervals().size() * sizeof(z13::station::rooms::FreeInterval) +
                               (graph.columns.ColumnCount() + 1) * sizeof(size_t);
  const auto column_rooms = ColumnRooms(graph);
  const std::vector<Eigen::Vector3i> cells = RandomCells(graph);
  size_t free_cells {};
  const double column_queries_ms = Ms([&] {
    for (const Eigen::Vector3i& cell : cells) {
      free_cells += graph.RoomAt(cell).has_value() ? 1 : 0;
    }
  });
  std::cout << std::format(
      "[ bench ] '{}' columns: {} sealed boxes, {} rooms, {} intervals, {:.1f} MB, built in {:.0f} ms, "
      "{} RoomAt in {:.1f} ms\n",
      name, sources.sealed.size(), column_rooms.size(), graph.columns.Intervals().size(),
      static_cast<double>(columns_bytes) / kBytesPerMb, columns_ms, kQueries, column_queries_ms);

  for (const int chunk_cells : kChunkSizes) {
    std::optional<partition::KdPartition> kd;
    const double build_ms = Ms([&] { kd.emplace(sources.sealed, chunk_cells); });
    partition::KdRooms rooms;
    const double flood_ms = Ms([&] { rooms = kd->FloodRooms(); });
    const auto kd_rooms = KdRoomsByAnchor(rooms);
    size_t kd_free_cells {};
    const double kd_queries_ms = Ms([&] {
      for (const Eigen::Vector3i& cell : cells) {
        kd_free_cells += kd->LeafAt(cell).has_value() ? 1 : 0;
      }
    });
    size_t portal_leaves {};
    const double portals_ms = Ms([&] {
      for (const auto& portal : sources.portals) {
        // The opening itself is sealed for a window: the cells on its two sides are what count.
        CellBox sides = portal.opening;
        const auto axis = static_cast<Eigen::Index>(portal.axis);
        --sides.min[axis];
        sides.extent[axis] += 2;
        portal_leaves += kd->LeavesIn(sides).size();
      }
    });
    std::vector<double> rebuilds;
    const size_t stride = std::max<size_t>(1, sources.sealed.size() / kRebuiltChunks);
    for (size_t i = 0; i < sources.sealed.size(); i += stride) {
      rebuilds.push_back(Ms([&] { kd->Rebuild(sources.sealed, sources.sealed[i]); }));
    }
    std::ranges::sort(rebuilds);
    EXPECT_EQ(kd_rooms, column_rooms) << name << ", chunk " << chunk_cells;
    EXPECT_EQ(kd_free_cells, free_cells) << name << ", chunk " << chunk_cells;
    std::cout << std::format(
        "[ bench ] '{}' kd chunk {}: {} chunks, {} free leaves, {:.2f} MB, built in {:.0f} ms, "
        "flood {:.0f} ms, rooms {}, {} LeafAt in {:.1f} ms, both sides of every portal {:.1f} ms ({} leaves), "
        "rebuild of one block's chunks median/max {:.1f}/{:.1f} ms\n",
        name, chunk_cells, kd->ChunkCount(), kd->LeafCount(),
        static_cast<double>(kd->Bytes()) / kBytesPerMb, build_ms, flood_ms,
        kd_rooms == column_rooms ? "match" : "DIFFER", kQueries, kd_queries_ms, portals_ms, portal_leaves,
        rebuilds[rebuilds.size() / 2], rebuilds.back())
              << std::flush;
  }
}

namespace bg = boost::geometry;
namespace bgi = boost::geometry::index;

using ChunkLeaves = std::vector<std::vector<partition::LeafEntry>>;

// Whether the cell lies in the leaf's half-open box [min, max).
bool HoldsCell(const partition::LeafBox& box, const partition::CellPoint& cell) {
  return bg::get<0>(cell) < bg::get<bg::max_corner, 0>(box) && bg::get<1>(cell) < bg::get<bg::max_corner, 1>(box) &&
         bg::get<2>(cell) < bg::get<bg::max_corner, 2>(box);
}

// One chunk's leaves, R-trees of `Params`, and the queries rooms make of them.
template <typename Params>
void MeasureTrees(const std::string& label, const partition::KdPartition& kd, const std::vector<Eigen::Vector3i>& cells) {
  using Tree = bgi::rtree<partition::LeafEntry, Params>;
  std::unordered_map<Eigen::Vector3i, size_t, primitives::CellHash> index_of_chunk;
  ChunkLeaves leaves;
  for (const auto& [key, chunk] : kd.Chunks()) {
    index_of_chunk[key] = leaves.size();
    std::vector<partition::LeafEntry>& entries = leaves.emplace_back();
    for (uint32_t i = 0; i < chunk.leaves.size(); ++i) {
      const CellBox& leaf = chunk.leaves[i];
      entries.emplace_back(partition::LeafBox({leaf.min.x(), leaf.min.y(), leaf.min.z()},
                                              {leaf.End().x(), leaf.End().y(), leaf.End().z()}),
                           i);
    }
  }
  std::vector<Tree> packed;
  const double packed_ms = Ms([&] {
    for (const auto& entries : leaves) {
      packed.emplace_back(entries);
    }
  });
  std::vector<Tree> inserted(leaves.size());
  const double inserted_ms = Ms([&] {
    for (size_t i = 0; i < leaves.size(); ++i) {
      for (const partition::LeafEntry& entry : leaves[i]) {
        inserted[i].insert(entry);
      }
    }
  });
  const auto tree_of = [&](const Eigen::Vector3i& cell) -> std::optional<std::reference_wrapper<const Tree>> {
    const auto found = index_of_chunk.find(primitives::ChunkOf(cell, kd.ChunkCells()));
    if (found == index_of_chunk.end()) {
      return std::nullopt;
    }
    return std::cref(packed[found->second]);
  };
  size_t by_box {};
  const double box_ms = Ms([&] {
    for (const Eigen::Vector3i& cell : cells) {
      if (const auto tree = tree_of(cell)) {
        const partition::LeafBox query({cell.x(), cell.y(), cell.z()}, {cell.x() + 1, cell.y() + 1, cell.z() + 1});
        tree->get().query(bgi::contains(query), boost::make_function_output_iterator([&](const auto&) { ++by_box; }));
      }
    }
  });
  size_t by_point {};
  const double point_ms = Ms([&] {
    for (const Eigen::Vector3i& cell : cells) {
      if (const auto tree = tree_of(cell)) {
        const partition::CellPoint point(cell.x(), cell.y(), cell.z());
        tree->get().query(bgi::intersects(point) && bgi::satisfies([&point](const partition::LeafEntry& entry) {
                      return HoldsCell(entry.first, point);
                    }),
                    boost::make_function_output_iterator([&](const auto&) { ++by_point; }));
      }
    }
  });
  size_t neighbours {};
  const double neighbours_ms = Ms([&] {
    for (size_t i = 0; i < leaves.size(); ++i) {
      for (const partition::LeafEntry& entry : leaves[i]) {
        packed[i].query(bgi::intersects(entry.first),
                        boost::make_function_output_iterator([&](const auto&) { ++neighbours; }));
      }
    }
  });
  EXPECT_EQ(by_box, by_point) << label;
  std::cout << std::format(
      "[ bench ] rtree {:<14} packed {:6.1f} ms, inserted {:6.1f} ms, {} LeafAt by box {:6.1f} ms / by point {:6.1f} ms, "
      "leaf neighbours {:6.1f} ms ({})\n",
      label, packed_ms, inserted_ms, cells.size(), box_ms, point_ms, neighbours_ms, neighbours)
            << std::flush;
}

TEST(PartitionBench, DISABLED_RtreeVariants) {
  const primitives::Palette palette = ShippedPalette().value();
  const RoomSources sources = ShippedSources("spire", palette);
  const RoomGraph graph = z13::station::rooms::BuildRooms(sources).value();
  const partition::KdPartition kd(sources.sealed, kChunkSizes[0]);
  const std::vector<Eigen::Vector3i> cells = RandomCells(graph);
  std::cout << std::format("[ bench ] 'spire' chunk {}: {} chunks, {} leaves\n", kd.ChunkCells(), kd.ChunkCount(),
                           kd.LeafCount());
  MeasureTrees<bgi::linear<4>>("linear<4>", kd, cells);
  MeasureTrees<bgi::linear<8>>("linear<8>", kd, cells);
  MeasureTrees<bgi::linear<16>>("linear<16>", kd, cells);
  MeasureTrees<bgi::quadratic<4>>("quadratic<4>", kd, cells);
  MeasureTrees<bgi::quadratic<8>>("quadratic<8>", kd, cells);
  MeasureTrees<bgi::quadratic<16>>("quadratic<16>", kd, cells);
  MeasureTrees<bgi::rstar<4>>("rstar<4>", kd, cells);
  MeasureTrees<bgi::rstar<8>>("rstar<8>", kd, cells);
  MeasureTrees<bgi::rstar<16>>("rstar<16>", kd, cells);
}

TEST(PartitionBench, DISABLED_ColumnsAgainstKdTree) {
  const primitives::Palette palette = ShippedPalette().value();
  for (const std::string& scene : primitives::BlueprintScenes(SourceAsset({}))) {
    Measure(scene, ShippedSources(scene, palette));
  }
  Measure("oblique", RoomSources {.sealed = ObliqueScene()});
}

}  // namespace
}  // namespace z13::testing
