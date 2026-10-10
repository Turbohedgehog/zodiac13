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

#include <lib_core/utils/frustum.h>

#include <algorithm>

namespace z13::math {

Frustum::Frustum(const Eigen::Matrix4f& view_projection) : Frustum(view_projection, FullScreen()) {
}

// Gribb-Hartmann, with the side planes moved in: x >= low * w is (row - low * w) · p >= 0.
Frustum::Frustum(const Eigen::Matrix4f& view_projection, const Eigen::AlignedBox2f& ndc) {
  const Eigen::Vector4f w = view_projection.row(3).transpose();
  for (int axis = 0; axis < 2; ++axis) {
    const Eigen::Vector4f row = view_projection.row(axis).transpose();
    planes_[2 * axis] = row - (ndc.min()[axis] * w);
    planes_[(2 * axis) + 1] = (ndc.max()[axis] * w) - row;
  }
  const Eigen::Vector4f depth = view_projection.row(2).transpose();
  planes_[4] = w + depth;
  planes_[5] = w - depth;
}

Eigen::AlignedBox2f Frustum::FullScreen() {
  return {Eigen::Vector2f::Constant(-1.f), Eigen::Vector2f::Constant(1.f)};
}

bool Frustum::Intersects(const Eigen::AlignedBox3f& box) const {
  return std::ranges::none_of(planes_, [&box](const Eigen::Vector4f& plane) {
    const Eigen::Vector3f normal = plane.head<3>();
    const Eigen::Vector3f farthest_inside = (normal.array() >= 0.f).select(box.max(), box.min());
    return normal.dot(farthest_inside) + plane.w() < 0.f;
  });
}

}  // namespace z13::math
