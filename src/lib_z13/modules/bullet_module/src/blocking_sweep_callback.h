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

#include <optional>

#include <bullet/btBulletDynamicsCommon.h>

namespace z13::bullet_module {

struct SweepHit {
  btScalar fraction {};
  btVector3 normal;
};

// The closest hit that blocks the motion, ties settled by hit_order.h; surfaces it moves away
// from (normal along the motion) are ignored, so a sphere resting against a block can leave it.
class BlockingSweepCallback : public btCollisionWorld::ConvexResultCallback {
 public:
  explicit BlockingSweepCallback(const btVector3& motion);

  btScalar addSingleResult(btCollisionWorld::LocalConvexResult& result, bool normal_in_world_space) override;

  const std::optional<SweepHit>& Hit() const { return hit_; }

 private:
  btVector3 motion_;
  std::optional<SweepHit> hit_;
};

}  // namespace z13::bullet_module
