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

#include <primitives/lights.h>

namespace z13::station::light {

// A lamp as the cell light takes it, in cells.
struct Lamp {
  Eigen::Vector3f position = Eigen::Vector3f::Zero();
  // In 0..1 per channel, scaled by the intensity.
  Eigen::Vector3f color = Eigen::Vector3f::Zero();
  float radius {};

  bool operator==(const Lamp&) const = default;
};

Lamp LampOf(const z13::building::primitives::PointLight& light);

}  // namespace z13::station::light
