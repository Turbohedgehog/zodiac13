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

#pragma once

#include <span>
#include <unordered_map>
#include <vector>

#include <Eigen/Dense>

#include <z13/components/station.h>
#include <z13_primitives/geometry.h>
#include <z13_primitives/palette.h>
#include <z13_primitives/placement.h>

// Blocks merged into one mesh per render chunk and material, so a station draws in hundreds
// of calls instead of one per block.
namespace z13::building::primitives {

// Blocks filed by the chunk of `chunk_cells`³ cells holding their lowest cell.
using ChunkBlocks = std::unordered_map<Eigen::Vector3i, std::vector<z13::station::Block>, CellHash>;

ChunkBlocks GroupByChunk(std::span<const z13::station::Block> blocks, int chunk_cells);

// Puts `blocks` in one fixed order, so two lists of the same blocks compare equal.
void SortBlocks(std::vector<z13::station::Block>& blocks);

// The primitive's checker; a type the palette lacks, or no palette, gets grey.
Checker MaterialOf(OptionalPalette palette, uint32_t type_id);

struct MaterialMesh {
  Checker material;
  Mesh mesh;
};

// Positions in cells, already placed in the world.
struct ChunkMesh {
  std::vector<MaterialMesh> parts;
  CellBox bounds;
};

ChunkMesh BuildChunkMesh(std::span<const z13::station::Block> blocks, OptionalPalette palette);

}  // namespace z13::building::primitives
