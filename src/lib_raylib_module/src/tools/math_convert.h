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
#include <raylib.h>

#include <z13/components/color.h>

namespace z13::raylib {

// A full channel of z13::Rgba and of raylib's Color.
inline constexpr float kMaxColorChannel = 255.f;

// 0-255 channels as 0-1.
Eigen::Vector4f ToUnitColor(const z13::Rgba& rgba);
::Vector3 EigenToRaylibVector(const Eigen::Vector3f& v);

// Z-up world transform (column 0 = forward, column 2 = up) -> raylib camera pose.
// Keeps `fovy` / `projection` intact.
void UpdateCameraFromTransform(::Camera3D& camera, const Eigen::Matrix4f& transform);

// Column-major Eigen matrix -> raylib Matrix (also column-major internally).
::Matrix EigenToRaylibMatrix(const Eigen::Matrix4f& m);
Eigen::Matrix4f RaylibToEigenMatrix(const ::Matrix& m);

}  // namespace z13::raylib
