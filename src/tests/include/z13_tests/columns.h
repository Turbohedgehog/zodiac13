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

#include <Eigen/Dense>

#include <primitives/placement.h>

namespace z13::testing {

// Hashes an (x, y) column of cells, for tests keying sets and maps by `Eigen::Vector2i`.
struct ColumnHash {
  size_t operator()(const Eigen::Vector2i& column) const {
    return z13::building::primitives::CellHash {}({column.x(), column.y(), 0});
  }
};

}  // namespace z13::testing
