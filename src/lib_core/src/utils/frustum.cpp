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

namespace {

// Rows of the view-projection: x and y (the screen's axes), depth, then w.
constexpr int kScreenAxes = Eigen::AlignedBox2f::AmbientDimAtCompileTime;
constexpr int kDepthRow = 2;
constexpr int kWRow = 3;
// A low and a high plane on each axis.
constexpr int kPlanesPerAxis = 2;
constexpr int kSpaceAxes = 3;

// How much of a box has to be on a plane's inner side.
enum class Reach { kAny, kAll };

// The corner farthest along the plane's normal decides whether any of the box is inside, the
// nearest whether all of it is.
bool InFront(const Eigen::Vector4f& plane, const float* low, const float* high, Reach reach = Reach::kAny) {
  const float* p = plane.data();
  float distance = p[kWRow];
  for (int axis = 0; axis < kSpaceAxes; ++axis) {
    const bool toward_high = (p[axis] >= 0.f) == (reach == Reach::kAny);
    distance += p[axis] * (toward_high ? high[axis] : low[axis]);
  }
  return distance >= 0.f;
}

}  // namespace

Frustum::Frustum(const Eigen::Matrix4f& view_projection) : Frustum(view_projection, FullScreen()) {
}

// Gribb-Hartmann, with the side planes moved in: x >= low * w is (row - low * w) · p >= 0.
Frustum::Frustum(const Eigen::Matrix4f& view_projection, const Eigen::AlignedBox2f& ndc) {
  const Eigen::Vector4f w = view_projection.row(kWRow).transpose();
  for (int axis = 0; axis < kScreenAxes; ++axis) {
    const Eigen::Vector4f row = view_projection.row(axis).transpose();
    planes_[kPlanesPerAxis * axis] = row - (ndc.min()[axis] * w);
    planes_[(kPlanesPerAxis * axis) + 1] = (ndc.max()[axis] * w) - row;
  }
  const Eigen::Vector4f depth = view_projection.row(kDepthRow).transpose();
  planes_[kPlanesPerAxis * kDepthRow] = w + depth;
  planes_[(kPlanesPerAxis * kDepthRow) + 1] = w - depth;
}

Eigen::AlignedBox2f Frustum::FullScreen() {
  return {Eigen::Vector2f::Constant(-1.f), Eigen::Vector2f::Constant(1.f)};
}

// Plain loops over the raw floats: culling runs on every chunk and prop of every frame, and Eigen's
// expression temporaries cost tens of times as much in a Debug build.
bool Frustum::Intersects(const Eigen::AlignedBox3f& box) const {
  const float* low = box.min().data();
  const float* high = box.max().data();
  for (const Eigen::Vector4f& plane : planes_) {
    if (!InFront(plane, low, high)) {
      return false;
    }
  }
  return true;
}

bool Frustum::Contains(const Eigen::AlignedBox3f& box) const {
  const float* low = box.min().data();
  const float* high = box.max().data();
  return std::ranges::all_of(planes_, [&](const Eigen::Vector4f& plane) { return InFront(plane, low, high, Reach::kAll); });
}

bool Frustum::IntersectsSides(const std::array<float, 3>& low, const std::array<float, 3>& high) const {
  for (int plane = 0; plane < kScreenAxes * kPlanesPerAxis; ++plane) {
    if (!InFront(planes_[plane], low.data(), high.data())) {
      return false;
    }
  }
  return true;
}

}  // namespace z13::math
