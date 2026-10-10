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

#include <lib_core/utils/camera_pose.h>

#include <array>
#include <charconv>
#include <cmath>
#include <format>
#include <ranges>
#include <system_error>

#include <lib_core/utils/math.h>

namespace z13 {

namespace {

constexpr size_t kFields = 5;

}  // namespace

CameraPose PoseLookingAt(const Eigen::Vector3f& eye, const Eigen::Vector3f& target) {
  const Eigen::Vector3f look = target - eye;
  return {
      .eye = eye,
      .look = {
          .yaw_deg = math::ToDegrees(std::atan2(look.y(), look.x())),
          .pitch_deg = math::ToDegrees(std::atan2(-look.z(), look.head<2>().norm())),
      },
  };
}

Eigen::Vector3f Forward(const CameraPose& pose) {
  const float yaw = math::ToRadians(pose.look.yaw_deg);
  const float pitch = math::ToRadians(pose.look.pitch_deg);
  return {std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), -std::sin(pitch)};
}

std::expected<CameraPose, std::string> ParseCameraPose(std::string_view text) {
  std::array<float, kFields> values {};
  size_t count = 0;
  for (const auto part : std::views::split(text, ',')) {
    const std::string_view field(part.begin(), part.end());
    if (count == kFields) {
      return std::unexpected(std::format("'{}' has more than {} numbers", text, kFields));
    }
    const auto [end, error] = std::from_chars(field.data(), field.data() + field.size(), values[count]);
    if (error != std::errc {} || end != field.data() + field.size() || !std::isfinite(values[count])) {
      return std::unexpected(std::format("'{}' is not a number in '{}'", field, text));
    }
    ++count;
  }
  if (count != kFields) {
    return std::unexpected(std::format("'{}' needs x,y,z,yaw,pitch", text));
  }
  return CameraPose {
      .eye = {values[0], values[1], values[2]},
      .look = {.yaw_deg = values[3], .pitch_deg = values[4]},
  };
}

}  // namespace z13
