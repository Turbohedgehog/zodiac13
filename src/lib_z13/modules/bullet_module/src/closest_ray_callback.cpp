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

#include "closest_ray_callback.h"

#include <array>
#include <cstdint>

#include "hit_order.h"

namespace z13::bullet_module {

namespace {

std::array<uint32_t, 7> KeyOf(const RayHit& hit) {
  const VectorKey normal = z13::bullet_module::KeyOf(hit.normal);
  const VectorKey origin = z13::bullet_module::KeyOf(hit.object.get().getWorldTransform().getOrigin());
  return {Bits(hit.fraction), normal[0], normal[1], normal[2], origin[0], origin[1], origin[2]};
}

}  // namespace

// m_closestHitFraction stays 1, as in BlockingSweepCallback.
btScalar ClosestRayCallback::addSingleResult(btCollisionWorld::LocalRayResult& result, bool normal_in_world_space) {
  const btVector3 normal = normal_in_world_space
      ? result.m_hitNormalLocal
      : result.m_collisionObject->getWorldTransform().getBasis() * result.m_hitNormalLocal;
  const RayHit hit {.fraction = result.m_hitFraction, .normal = normal, .object = *result.m_collisionObject};
  if (!hit_ || KeyOf(hit) < KeyOf(*hit_)) {
    hit_ = hit;
  }
  return m_closestHitFraction;
}

}  // namespace z13::bullet_module
