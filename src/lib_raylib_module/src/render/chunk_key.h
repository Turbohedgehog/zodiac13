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

#include <cstddef>

#include <Eigen/Dense>

#include <z13/components/station/block.h>
#include <primitives/cell_box.h>

namespace z13::raylib {

// A render chunk: the chunk of cells in the axes of one grid.
struct ChunkKey {
  z13::station::GridId grid = z13::station::kMainGrid;
  Eigen::Vector3i chunk = Eigen::Vector3i::Zero();

  bool operator==(const ChunkKey&) const = default;
};

struct ChunkKeyHash {
  size_t operator()(const ChunkKey& key) const {
    return z13::building::primitives::CellHash {}(key.chunk) * 31 + static_cast<size_t>(key.grid);
  }
};

}  // namespace z13::raylib
