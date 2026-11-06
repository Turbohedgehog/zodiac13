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
#include <cstdint>

#include <Eigen/Dense>

namespace z13::building::primitives {

inline constexpr int kCellAxes = Eigen::Vector3i::SizeAtCompileTime;

// The cells [min, min + extent).
struct CellBox {
  Eigen::Vector3i min = Eigen::Vector3i::Zero();
  Eigen::Vector3i extent = Eigen::Vector3i::Zero();

  Eigen::Vector3i End() const { return min + extent; }
  bool Contains(const Eigen::Vector3i& cell) const;
  bool Overlaps(const CellBox& other) const;
  // The cells both boxes hold; an empty box (zero extent) if they share none.
  CellBox Intersection(const CellBox& other) const;
  // The box with `cells` more cells on both sides along each axis.
  CellBox Grown(const Eigen::Vector3i& cells) const;

  bool operator==(const CellBox&) const = default;
};

struct CellHash {
  size_t operator()(const Eigen::Vector3i& cell) const;
};

// The chunk of `chunk_cells`³ cells holding `cell`, by floor division so negative cells land
// in the chunk below zero.
Eigen::Vector3i ChunkOf(const Eigen::Vector3i& cell, int chunk_cells);

// A chunk as one number, 21 bits per axis; UnpackChunk gives the chunk back.
uint64_t PackChunk(const Eigen::Vector3i& chunk);
Eigen::Vector3i UnpackChunk(uint64_t packed);

}  // namespace z13::building::primitives
