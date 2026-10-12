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


#include "numbered_leaves.h"

#include <cstdint>

namespace z13::station::rooms {

NumberedLeaves::NumberedLeaves(const FreeSpace& space) {
  for (const Eigen::Vector3i& key : space.OrderedChunks()) {
    first_of_chunk_.emplace(key, leaves_.size());
    const auto count = static_cast<uint32_t>(space.AllChunks().at(key)->leaves.size());
    for (uint32_t leaf = 0; leaf < count; ++leaf) {
      leaves_.push_back({.chunk = key, .leaf = leaf});
    }
  }
}

}  // namespace z13::station::rooms
