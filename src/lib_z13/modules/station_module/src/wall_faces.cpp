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

#include "wall_faces.h"

#include <functional>

#include <primitives/geometry.h>

namespace z13::station::outline {

namespace {

constexpr float kAxisAligned = 0.999f;

Point DoubleAreaVector(const Polygon& polygon) {
  Point sum = Point::Zero();
  for (size_t i = 0; i < polygon.size(); ++i) {
    sum += polygon[i].cross(polygon[(i + 1) % polygon.size()]);
  }
  return sum;
}

Point NormalOf(const Polygon& polygon) {
  return DoubleAreaVector(polygon).normalized();
}

}  // namespace

size_t FloorSpotHash::operator()(const FloorSpot& spot) const {
  return z13::building::primitives::CellHash {}(spot.cell) * 31 + static_cast<size_t>(spot.level);
}

float AreaOf(const Polygon& polygon) {
  return DoubleAreaVector(polygon).norm() / 2.0f;
}

Polygon KeepAbove(const Polygon& polygon, const Point& normal, float level) {
  Polygon kept;
  for (size_t i = 0; i < polygon.size(); ++i) {
    const Point& from = polygon[i];
    const Point& to = polygon[(i + 1) % polygon.size()];
    const float from_gap = normal.dot(from) - level;
    const float to_gap = normal.dot(to) - level;
    if (from_gap >= 0.0f) {
      kept.push_back(from);
    }
    if ((from_gap >= 0.0f) != (to_gap >= 0.0f)) {
      kept.push_back(from + (from_gap / (from_gap - to_gap)) * (to - from));
    }
  }
  return kept;
}

std::optional<Polygon> Solid(Polygon polygon) {
  if (polygon.size() < 3 || AreaOf(polygon) < kMinArea) {
    return std::nullopt;
  }
  return polygon;
}

BlockFaces FacesOf(const Block& block, const z13::building::primitives::Palette& palette) {
  namespace primitives = z13::building::primitives;
  BlockFaces result;
  const primitives::CellPose pose = primitives::PoseOf(block);
  const Eigen::Matrix3f rotation = pose.rotation.cast<float>();
  const Point origin = pose.origin.cast<float>();
  for (const primitives::ConvexSolid& solid :
       primitives::BlockSolids(std::cref(palette), primitives::ShapeKeyOf(block.spec))) {
    for (const std::vector<uint32_t>& indices : solid.faces) {
      Face face;
      for (const uint32_t index : indices) {
        face.polygon.push_back(origin + rotation * solid.vertices[index]);
      }
      face.normal = NormalOf(face.polygon);
      result.oblique = result.oblique || face.normal.cwiseAbs().maxCoeff() < kAxisAligned;
      result.faces.push_back(std::move(face));
    }
  }
  return result;
}

}  // namespace z13::station::outline
