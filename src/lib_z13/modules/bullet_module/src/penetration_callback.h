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
#include <vector>

#include <bullet/btBulletDynamicsCommon.h>

namespace z13::bullet_module {

// Collects how far each penetrated block pushes a probe out; m_normalWorldOnB points from
// object B to object A, so the push goes along it or against it, by which one the probe is.
class PenetrationCallback : public btCollisionWorld::ContactResultCallback {
 public:
  explicit PenetrationCallback(const btCollisionObject& probe);

  btScalar addSingleResult(btManifoldPoint& point, const btCollisionObjectWrapper* wrap_a, int, int,
                           const btCollisionObjectWrapper*, int, int) override;

  // Summed sorted, not in the broadphase's order: float addition isn't associative.
  btVector3 Correction();

 private:
  std::reference_wrapper<const btCollisionObject> probe_;
  std::vector<btVector3> pushes_;
};

}  // namespace z13::bullet_module
