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

#include <cstdint>
#include <cstddef>
#include <expected>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <Eigen/Dense>

#include <primitives/palette.h>

// Geometry of a primitive in its own frame, in cells: it spans [0, size] on each axis,
// Z up. The caller scales by the cell size and applies the block's placement.
namespace z13::building::primitives {

// Opening a door removes only its leaf's collision.
enum class PartRole : uint8_t { kBody, kDoorLeaf };

// One convex piece of a shape; both its mesh and its collision hull come from these.
struct ConvexSolid {
  std::vector<Eigen::Vector3f> vertices;
  // Vertex indices of each planar face, counterclockwise seen from outside.
  std::vector<std::vector<uint32_t>> faces;
  PartRole role {};
};

// Flat-shaded triangle list: three entries per triangle in every array.
struct Mesh {
  std::vector<Eigen::Vector3f> positions;
  std::vector<Eigen::Vector3f> normals;
};

std::expected<std::vector<ConvexSolid>, std::string> BuildSolids(const Shape& shape, const Eigen::Vector3i& size);

Mesh BuildMesh(std::span<const ConvexSolid> solids);

// Blocks of one primitive at one size share their geometry, so caches are keyed by this.
struct BlockShapeKey {
  uint32_t type_id {};
  Eigen::Vector3i size = Eigen::Vector3i::Zero();

  bool operator==(const BlockShapeKey&) const = default;
};

struct BlockShapeKeyHash {
  size_t operator()(const BlockShapeKey& key) const;
};

using OptionalPalette = std::optional<std::reference_wrapper<const Palette>>;

// The primitive's solids; a type the palette lacks, or no palette, gives a plain box so the
// block still collides and draws.
std::vector<ConvexSolid> BlockSolids(OptionalPalette palette, const BlockShapeKey& shape);

std::vector<ConvexSolid> Scaled(std::vector<ConvexSolid> solids, float factor);

}  // namespace z13::building::primitives
