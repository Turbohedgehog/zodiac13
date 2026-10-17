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

#include "penetration_callback.h"

#include <algorithm>
#include <numeric>

#include "hit_order.h"

namespace z13::bullet_module {

PenetrationCallback::PenetrationCallback(const btCollisionObject& probe) : probe_(probe) {
}

btScalar PenetrationCallback::addSingleResult(btManifoldPoint& point, const btCollisionObjectWrapper* wrap_a, int,
                                              int, const btCollisionObjectWrapper*, int, int) {
  if (point.getDistance() >= 0.f) {
    return 0.f;
  }
  const btVector3 push = point.m_normalWorldOnB * -point.getDistance();
  pushes_.push_back(wrap_a->getCollisionObject() == &probe_.get() ? push : -push);
  return 0.f;
}

btVector3 PenetrationCallback::Correction() {
  std::ranges::sort(pushes_, {}, KeyOf);
  return std::accumulate(pushes_.begin(), pushes_.end(), btVector3(0.f, 0.f, 0.f));
}

}  // namespace z13::bullet_module
