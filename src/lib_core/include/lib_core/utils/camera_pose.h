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

#include <expected>
#include <string>
#include <string_view>

#include <Eigen/Dense>

namespace z13 {

// Kept rather than re-derived via eulerAngles(), which leaked roll near +/-90 deg pitch.
// Yaw around +Z from +X, pitch positive looking down, in degrees.
struct LookAngles {
  using State = void;
  float yaw_deg {};
  float pitch_deg {};
};

// Where a camera stands, in meters, and looks.
struct CameraPose {
  Eigen::Vector3f eye = Eigen::Vector3f::Zero();
  LookAngles look;
};

CameraPose PoseLookingAt(const Eigen::Vector3f& eye, const Eigen::Vector3f& target);

// A unit vector.
Eigen::Vector3f Forward(const CameraPose& pose);

// "x,y,z,yaw,pitch".
std::expected<CameraPose, std::string> ParseCameraPose(std::string_view text);

}  // namespace z13
