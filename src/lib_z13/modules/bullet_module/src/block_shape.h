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

#include <memory>
#include <span>
#include <vector>

#include <bullet/btBulletDynamicsCommon.h>

#include <z13_primitives/geometry.h>

namespace z13::bullet_module {

// A compound of one child per convex piece: axis-aligned boxes as btBoxShape (exact, no
// margin outside), anything else as a convex hull.
class BlockShape {
 public:
  explicit BlockShape(std::span<const z13::primitives::ConvexSolid> solids);
  BlockShape(const BlockShape&) = delete;
  BlockShape& operator=(const BlockShape&) = delete;

  btCompoundShape& Shape() { return compound_; }

 private:
  // Declaration order matters: compound_ points into children_ and goes first on teardown.
  std::vector<std::unique_ptr<btCollisionShape>> children_;
  btCompoundShape compound_;
};

}  // namespace z13::bullet_module
