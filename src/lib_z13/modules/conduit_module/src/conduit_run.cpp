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


#include "conduit_run.h"

#include <algorithm>
#include <cstdlib>
#include <utility>

namespace z13::conduit {

namespace {

Eigen::Vector3i Step(ConduitAxis axis) {
  return Eigen::Vector3i::Unit(std::to_underlying(axis));
}

}  // namespace

Conduit RunBetween(uint32_t kind, const Eigen::Vector3i& from, const Eigen::Vector3i& to) {
  Eigen::Index axis = 0;
  (to - from).cwiseAbs().maxCoeff(&axis);
  Eigen::Vector3i start = from;
  start[axis] = std::min(from[axis], to[axis]);
  return {.kind = kind,
          .cell = start,
          .axis = static_cast<ConduitAxis>(axis),
          .length = std::abs(to[axis] - from[axis]) + 1};
}

std::vector<Eigen::Vector3i> CellsOf(const Conduit& conduit) {
  std::vector<Eigen::Vector3i> cells;
  cells.reserve(static_cast<size_t>(conduit.length));
  for (int i = 0; i < conduit.length; ++i) {
    cells.emplace_back(conduit.cell + Step(conduit.axis) * i);
  }
  return cells;
}

}  // namespace z13::conduit
