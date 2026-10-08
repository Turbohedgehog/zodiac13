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

#include <gtest/gtest.h>

#include <cstdint>
#include <numeric>
#include <string>
#include <vector>

#include <Eigen/Dense>

#include <primitives/chunk_mesh.h>
#include <z13_tests/shipped_station.h>

namespace z13::building::primitives {
namespace {

using z13::station::Block;
using z13::station::Orientation;

// assets/station/palette.json
constexpr uint32_t kWallId = 2;
constexpr uint32_t kDoorId = 3;
constexpr uint32_t kHiddenDoorId = 7;
constexpr int kChunkCells = 32;
const Eigen::Vector3i kPanel {8, 1, 8};

Block At(uint32_t type_id, const Eigen::Vector3i& cell, Orientation orientation = {}) {
  return {.spec = {.type_id = type_id, .size = kPanel, .orientation = orientation}, .cell = cell};
}

size_t VertexCount(const ChunkMesh& chunk) {
  return std::accumulate(chunk.parts.begin(), chunk.parts.end(), size_t {},
                         [](size_t sum, const MaterialMesh& part) { return sum + part.mesh.positions.size(); });
}

TEST(ChunkMeshTest, BlocksAreFiledByTheirLowestCell) {
  const std::vector<Block> blocks {At(kWallId, {0, 0, 0}), At(kWallId, {30, 0, 0}), At(kWallId, {32, 0, 0}),
                                   At(kWallId, {-1, 0, 0})};

  const ChunkBlocks chunks = GroupByChunk(blocks, kChunkCells);

  ASSERT_EQ(chunks.size(), 3U);
  EXPECT_EQ(chunks.at({0, 0, 0}).size(), 2U);
  EXPECT_EQ(chunks.at({1, 0, 0}).size(), 1U);
  EXPECT_EQ(chunks.at({-1, 0, 0}).size(), 1U);
}

TEST(ChunkMeshTest, SortedListsOfTheSameBlocksCompareEqual) {
  std::vector<Block> one {At(kWallId, {0, 0, 0}), At(kDoorId, {8, 0, 0}), At(kWallId, {0, 0, 8})};
  std::vector<Block> other {one[2], one[0], one[1]};

  SortBlocks(one);
  SortBlocks(other);

  EXPECT_EQ(one, other);
}

TEST(ChunkMeshTest, OneMeshPerMaterial) {
  const Palette palette = z13::testing::ShippedPalette().value();
  const std::vector<Block> blocks {At(kWallId, {0, 0, 0}), At(kHiddenDoorId, {8, 0, 0}), At(kDoorId, {16, 0, 0})};

  const ChunkMesh chunk = BuildChunkMesh(blocks, palette);

  ASSERT_EQ(chunk.parts.size(), 2U);
  EXPECT_EQ(chunk.parts[0].material, MaterialOf(palette, kWallId));
  EXPECT_EQ(chunk.parts[1].material, MaterialOf(palette, kDoorId));
}

TEST(ChunkMeshTest, EveryBlockIsPlacedWithinItsCells) {
  const Palette palette = z13::testing::ShippedPalette().value();
  const std::vector<Block> blocks {At(kWallId, {3, -5, 2}), At(kWallId, {20, 4, 0}, Orientation::kFacePosYUpPosZ)};

  for (const Block& block : blocks) {
    const ChunkMesh chunk = BuildChunkMesh(std::vector {block}, palette);
    const CellBox cells = OccupiedCells(block);
    EXPECT_EQ(chunk.bounds.min, cells.min);
    EXPECT_EQ(chunk.bounds.extent, cells.extent);
    Eigen::AlignedBox3f placed;
    for (const Eigen::Vector3f& position : chunk.parts.front().mesh.positions) {
      placed.extend(position);
    }
    EXPECT_TRUE(placed.min().isApprox(cells.min.cast<float>()));
    EXPECT_TRUE(placed.max().isApprox(cells.End().cast<float>()));
  }
}

// Each shipped station, split into chunks, keeps every block's triangles.
TEST(ChunkMeshTest, ChunksOfAShippedStationHoldEveryBlock) {
  const Palette palette = z13::testing::ShippedPalette().value();
  for (const std::string& scene : BlueprintScenes(z13::testing::SourceAsset({}))) {
    const std::vector<Block> blocks = z13::testing::ShippedBlueprint(scene, palette).value();
    size_t chunked = 0;
    for (const auto& [key, chunk_blocks] : GroupByChunk(blocks, kChunkCells)) {
      chunked += VertexCount(BuildChunkMesh(chunk_blocks, palette));
    }
    EXPECT_EQ(chunked, VertexCount(BuildChunkMesh(blocks, palette))) << scene;
  }
}

}  // namespace
}  // namespace z13::building::primitives
