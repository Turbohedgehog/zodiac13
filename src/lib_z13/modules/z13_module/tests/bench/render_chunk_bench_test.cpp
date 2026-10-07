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

// Render chunks of every shipped station (z13_primitives/chunk_mesh.h) at several chunk
// sizes, without the GPU upload. Built by `make.py --bench`, run with
// `z13.py --bench --filter 'RenderChunkBench.*'`.

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <format>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include <z13/components/station.h>
#include <z13_primitives/chunk_mesh.h>
#include <z13_primitives/draw_order.h>
#include <z13_primitives/station_assets.h>
#include <z13_tests/shipped_station.h>

namespace z13::testing {
namespace {

namespace primitives = z13::building::primitives;

constexpr int kChunkSizes[] = {16, 32, 64, 128};

template <class Work>
double Ms(Work work) {
  const auto start = std::chrono::steady_clock::now();
  work();
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

void Measure(const std::string& scene, const std::vector<z13::station::Block>& blocks,
             const primitives::Palette& palette, int chunk_cells) {
  std::vector<z13::station::Block> opaque;
  std::ranges::copy_if(blocks, std::back_inserter(opaque),
                       [&palette](const auto& block) { return !primitives::IsTransparent(block, palette); });
  size_t meshes = 0;
  size_t vertices = 0;
  double slowest_chunk_ms = 0;
  const double total_ms = Ms([&] {
    for (auto& [key, chunk_blocks] : primitives::GroupByChunk(opaque, chunk_cells)) {
      const double chunk_ms = Ms([&] {
        primitives::SortBlocks(chunk_blocks);
        const primitives::ChunkMesh chunk = primitives::BuildChunkMesh(chunk_blocks, palette);
        meshes += chunk.parts.size();
        for (const primitives::MaterialMesh& part : chunk.parts) {
          vertices += part.mesh.positions.size();
        }
      });
      slowest_chunk_ms = std::max(slowest_chunk_ms, chunk_ms);
    }
  });
  const size_t chunks = primitives::GroupByChunk(opaque, chunk_cells).size();
  std::cout << std::format(
      "[ bench ] '{}' chunk {} cells: {} opaque + {} glass blocks, {} chunks, {} meshes (draw calls before culling), "
      "{} vertices, all built in {:.0f} ms, slowest chunk {:.1f} ms\n",
      scene, chunk_cells, opaque.size(), blocks.size() - opaque.size(), chunks, meshes, vertices, total_ms,
      slowest_chunk_ms)
            << std::flush;
}

TEST(RenderChunkBench, DISABLED_ChunkSizes) {
  const primitives::Palette palette = ShippedPalette().value();
  for (const std::string& scene : primitives::BlueprintScenes(SourceAsset({}))) {
    const auto blocks = ShippedBlueprint(scene, palette);
    ASSERT_TRUE(blocks.has_value()) << blocks.error();
    for (const int chunk_cells : kChunkSizes) {
      Measure(scene, *blocks, palette, chunk_cells);
    }
  }
}

}  // namespace
}  // namespace z13::testing
