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

#include <functional>
#include <optional>

#include <bullet/btBulletDynamicsCommon.h>

namespace z13::bullet_module {

struct RayHit {
  btScalar fraction {};
  btVector3 normal;
  std::reference_wrapper<const btCollisionObject> object;
};

// The closest hit, ties settled by hit_order.h and then by where the bodies stand: entity ids
// differ between peers.
class ClosestRayCallback : public btCollisionWorld::RayResultCallback {
 public:
  btScalar addSingleResult(btCollisionWorld::LocalRayResult& result, bool normal_in_world_space) override;

  const std::optional<RayHit>& Hit() const { return hit_; }

 private:
  std::optional<RayHit> hit_;
};

}  // namespace z13::bullet_module
