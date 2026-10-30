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
#include <optional>
#include <vector>

#include <Eigen/Dense>

#include <primitives/palette.h>
#include <primitives/placement.h>
#include <z13/components/block.h>

namespace z13::station::outline {

using Cell = Eigen::Vector3i;
using Point = Eigen::Vector3f;
using Polygon = std::vector<Point>;

constexpr int kVerticalAxis = 2;
// The least cosine between a face's normal and the way to the room for the face to bound it.
constexpr float kFacing = 0.2f;
// The least area, in square cells, of a polygon worth outlining.
constexpr float kMinArea = 1e-3f;

struct Face {
  Polygon polygon;
  Point normal = Point::Zero();
};

struct BlockFaces {
  std::vector<Face> faces;
  bool oblique {};
};

struct FloorSpot {
  Cell cell;
  int level {};

  bool operator==(const FloorSpot&) const = default;
};

struct FloorSpotHash {
  size_t operator()(const FloorSpot& spot) const;
};

float AreaOf(const Polygon& polygon);

Polygon KeepAbove(const Polygon& polygon, const Point& normal, float level);

std::optional<Polygon> Solid(Polygon polygon);

BlockFaces FacesOf(const Block& block, const z13::building::primitives::Palette& palette);

}  // namespace z13::station::outline
