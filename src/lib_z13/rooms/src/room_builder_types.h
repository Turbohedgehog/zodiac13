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

#include <cstddef>
#include <cstdint>
#include <vector>

#include <Eigen/Dense>

namespace z13::station::rooms {

struct Span {
  int begin {};
  int end {};
};

struct Bounds {
  Eigen::Vector3i low;
  Eigen::Vector3i high;
};

// The z-spans the sealed boxes take in each column, in compressed rows, sorted by start.
struct SealedColumns {
  std::vector<size_t> offsets;
  std::vector<Span> spans;
};

// A room's extent while its intervals are gathered: plain ints, as this runs once per
// interval.
struct RoomExtent {
  int anchor_x {};
  int anchor_y {};
  int anchor_z {};
  int low_x {};
  int low_y {};
  int high_x {};
  int high_y {};
  int low_z {};
  int high_z {};
  int64_t volume {};
};

}  // namespace z13::station::rooms
