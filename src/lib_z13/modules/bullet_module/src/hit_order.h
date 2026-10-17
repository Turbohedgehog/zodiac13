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

#include <array>
#include <bit>
#include <cstdint>

#include <bullet/btBulletDynamicsCommon.h>

// Peers build their broadphase trees in different orders, so a query must not keep whichever
// of two equal hits Bullet reports first (Н3 in docs/net-core.md). Hits are ranked by their
// bits: the same order as the values for the non-negative fractions, and total for the rest.
namespace z13::bullet_module {

static_assert(sizeof(btScalar) == sizeof(uint32_t), "hit keys assume single-precision Bullet");

using VectorKey = std::array<uint32_t, 3>;

inline uint32_t Bits(btScalar value) {
  return std::bit_cast<uint32_t>(value);
}

inline VectorKey KeyOf(const btVector3& v) {
  return {Bits(v.x()), Bits(v.y()), Bits(v.z())};
}

}  // namespace z13::bullet_module
