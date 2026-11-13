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

#include <cstdint>

#include <Eigen/Dense>

namespace z13::station {

// A lift between two stops on the entity that holds its cabin's KinematicBody (z13/components/gameplay/kinematic_body.h).
// Using the Switch block at either button's cell sends an idle cabin to the other stop; the stops are the meters the
// cabin's lowest corner stands at. A lift that never moved stands at `lower`.
struct Lift {
  using State = void;
  Eigen::Vector3f lower = Eigen::Vector3f::Zero();
  Eigen::Vector3f upper = Eigen::Vector3f::Zero();
  // Cells of the main grid.
  Eigen::Vector3i lower_button = Eigen::Vector3i::Zero();
  Eigen::Vector3i upper_button = Eigen::Vector3i::Zero();
  uint32_t trip_ticks {};
};

}  // namespace z13::station
