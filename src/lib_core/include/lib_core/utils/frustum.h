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

#include <Eigen/Dense>

namespace z13::math {

// The six planes of a view frustum, for culling boxes against it.
class Frustum {
 public:
  // `view_projection` maps world points into OpenGL clip space (-w..w on every axis).
  explicit Frustum(const Eigen::Matrix4f& view_projection);

  // Conservative: a box near a corner of the frustum may pass though it lies outside.
  bool Intersects(const Eigen::AlignedBox3f& box) const;

 private:
  // (normal, offset): a point p is inside when normal·p + offset >= 0.
  std::array<Eigen::Vector4f, 6> planes_;
};

}  // namespace z13::math
