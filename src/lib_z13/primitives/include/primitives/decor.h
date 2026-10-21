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
#include <vector>

#include <Eigen/Dense>

namespace z13::building::primitives {

// A model standing in a station, in meters.
struct Prop {
  std::string model;
  Eigen::Vector3f position = Eigen::Vector3f::Zero();
  float yaw = 0.F;
  float pitch = 0.F;
};

// The props of a station and the primitives they stand in for.
struct Decor {
  std::vector<Prop> props;
  std::vector<std::string> replaces;
};

// Parses props JSON (decor.fbs).
std::expected<Decor, std::string> ParseDecor(std::string_view json);

}  // namespace z13::building::primitives
