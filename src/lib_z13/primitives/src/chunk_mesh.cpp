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

#include <primitives/chunk_mesh.h>

#include <algorithm>
#include <iterator>
#include <tuple>

namespace z13::building::primitives {

namespace {

constexpr Checker kUnknownMaterial {
    .first = {128, 128, 128, 255},
    .second = {100, 100, 100, 255},
};

CellBox Enclosing(const CellBox& a, const CellBox& b) {
  const Eigen::Vector3i min = a.min.cwiseMin(b.min);
  return {.min = min, .extent = a.End().cwiseMax(b.End()) - min};
}

void AppendPlaced(const Mesh& shape, const CellPose& pose, Mesh& target) {
  const Eigen::Matrix3f rotation = pose.rotation.cast<float>();
  const Eigen::Vector3f origin = pose.origin.cast<float>();
  std::ranges::transform(shape.positions, std::back_inserter(target.positions),
                         [&](const Eigen::Vector3f& p) -> Eigen::Vector3f { return rotation * p + origin; });
  std::ranges::transform(shape.normals, std::back_inserter(target.normals),
                         [&](const Eigen::Vector3f& n) -> Eigen::Vector3f { return rotation * n; });
}

}  // namespace

ChunkBlocks GroupByChunk(std::span<const z13::station::Block> blocks, int chunk_cells) {
  ChunkBlocks chunks;
  for (const z13::station::Block& block : blocks) {
    chunks[ChunkOf(OccupiedCells(block).min, chunk_cells)].push_back(block);
  }
  return chunks;
}

void SortBlocks(std::vector<z13::station::Block>& blocks) {
  const auto key = [](const z13::station::Block& b) {
    return std::tuple(b.cell.x(), b.cell.y(), b.cell.z(), b.spec.type_id, b.spec.orientation, b.spec.size.x(),
                      b.spec.size.y(), b.spec.size.z());
  };
  std::ranges::sort(blocks, {}, key);
}

Checker MaterialOf(OptionalPalette palette, uint32_t type_id) {
  const auto primitive = palette ? palette->get().Find(type_id) : std::nullopt;
  return primitive ? primitive->get().material : kUnknownMaterial;
}

ChunkMesh BuildChunkMesh(std::span<const z13::station::Block> blocks, OptionalPalette palette) {
  ChunkMesh chunk;
  if (blocks.empty()) {
    return chunk;
  }
  chunk.bounds = OccupiedCells(blocks.front());
  std::unordered_map<BlockShapeKey, Mesh, BlockShapeKeyHash> shapes;
  for (const z13::station::Block& block : blocks) {
    chunk.bounds = Enclosing(chunk.bounds, OccupiedCells(block));
    const BlockShapeKey key {.type_id = block.spec.type_id, .size = block.spec.size};
    auto shape = shapes.find(key);
    if (shape == shapes.end()) {
      shape = shapes.emplace(key, BuildMesh(BlockSolids(palette, key))).first;
    }
    const Checker material = MaterialOf(palette, block.spec.type_id);
    auto part = std::ranges::find(chunk.parts, material, &MaterialMesh::material);
    if (part == chunk.parts.end()) {
      part = chunk.parts.insert(chunk.parts.end(), MaterialMesh {.material = material});
    }
    AppendPlaced(shape->second, PoseOf(block), part->mesh);
  }
  return chunk;
}

}  // namespace z13::building::primitives
