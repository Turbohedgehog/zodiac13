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

#include <Eigen/Dense>

namespace z13::gravity {

// The acceleration of the room a body is in, zero in vacuum. State: rooms are rebuilt after
// movement, so a replayed tick must not read those of a later one.
struct Gravity {
  using State = void;
  Eigen::Vector3f acceleration = Eigen::Vector3f::Zero();

  bool Pulls() const { return !acceleration.isZero(); }
};

}  // namespace z13::gravity
