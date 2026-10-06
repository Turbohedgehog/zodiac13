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

#include "block_shape.h"

#include <algorithm>

#include <Eigen/Dense>

namespace z13::bullet_module {

namespace {

constexpr size_t kBoxCorners = 8;
// Bullet pads hulls by their margin; kept small so slopes sit where the mesh is drawn.
constexpr float kHullMargin = 1e-3f;

bool IsAxisAlignedBox(const z13::building::primitives::ConvexSolid& solid, const Eigen::AlignedBox3f& bounds) {
  if (solid.vertices.size() != kBoxCorners) {
    return false;
  }
  return std::ranges::all_of(solid.vertices, [&bounds](const Eigen::Vector3f& v) {
    return ((v.array() == bounds.min().array()) || (v.array() == bounds.max().array())).all();
  });
}

btVector3 ToBt(const Eigen::Vector3f& v) {
  return {v.x(), v.y(), v.z()};
}

}  // namespace

BlockShape::BlockShape(std::span<const z13::building::primitives::ConvexSolid> solids) {
  for (const z13::building::primitives::ConvexSolid& solid : solids) {
    Eigen::AlignedBox3f bounds;
    for (const Eigen::Vector3f& vertex : solid.vertices) {
      bounds.extend(vertex);
    }

    btTransform child_transform;
    child_transform.setIdentity();
    if (IsAxisAlignedBox(solid, bounds)) {
      children_.push_back(std::make_unique<btBoxShape>(ToBt(bounds.sizes() / 2.f)));
      child_transform.setOrigin(ToBt(bounds.center()));
    } else {
      auto hull = std::make_unique<btConvexHullShape>();
      for (const Eigen::Vector3f& vertex : solid.vertices) {
        hull->addPoint(ToBt(vertex), false);
      }
      hull->recalcLocalAabb();
      hull->setMargin(kHullMargin);
      children_.push_back(std::move(hull));
    }
    compound_.addChildShape(child_transform, children_.back().get());
  }
}

}  // namespace z13::bullet_module
