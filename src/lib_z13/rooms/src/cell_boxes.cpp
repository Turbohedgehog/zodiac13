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

#include "cell_boxes.h"

#include <algorithm>

namespace z13::station::rooms {

using z13::building::primitives::CellBox;
using z13::building::primitives::kCellAxes;

int64_t Volume(const CellBox& box) {
  return box.extent.cast<int64_t>().prod();
}

bool ShareFace(const CellBox& first, const CellBox& second) {
  int touching {};
  for (int axis = 0; axis < kCellAxes; ++axis) {
    const int overlap = std::min(first.End()[axis], second.End()[axis]) - std::max(first.min[axis], second.min[axis]);
    if (overlap < 0) {
      return false;
    }
    touching += overlap == 0 ? 1 : 0;
  }
  return touching == 1;
}

}  // namespace z13::station::rooms
