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

#include "math_convert.h"

#include <lib_core/math.h>

namespace z13::raylib {

void UpdateCameraFromTransform(::Camera3D& camera, const Eigen::Matrix4f& transform) {
  const Eigen::Vector3f position = z13::math::ExtractTranslation(transform);
  const Eigen::Quaternionf rotation = z13::math::ExtractQuat(transform);
  const Eigen::Vector3f forward = rotation * Eigen::Vector3f::UnitX();  // gameplay convention: local +X
  const Eigen::Vector3f up = rotation * Eigen::Vector3f::UnitZ();      // gameplay convention: local +Z
  const Eigen::Vector3f target = position + forward;

  camera.position = {position.x(), position.y(), position.z()};
  camera.target = {target.x(), target.y(), target.z()};
  camera.up = {up.x(), up.y(), up.z()};
}

::Matrix EigenToRaylibMatrix(const Eigen::Matrix4f& m) {
  const float* d = m.data();  // column-major: d[c * 4 + r] == m(r, c)
  return ::Matrix{
      d[0], d[4], d[8], d[12],
      d[1], d[5], d[9], d[13],
      d[2], d[6], d[10], d[14],
      d[3], d[7], d[11], d[15],
  };
}

}  // namespace z13::raylib
