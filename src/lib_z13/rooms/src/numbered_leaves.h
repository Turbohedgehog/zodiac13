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
#include <unordered_map>
#include <vector>

#include <Eigen/Dense>

#include <primitives/placement.h>
#include <rooms/free_space.h>

namespace z13::station::rooms {

// Every leaf of a free space, numbered chunk by chunk in OrderedChunks().
class NumberedLeaves {
 public:
  explicit NumberedLeaves(const FreeSpace& space);

  const std::vector<LeafRef>& Leaves() const { return leaves_; }
  size_t Number(const LeafRef& ref) const { return FirstOf(ref.chunk) + ref.leaf; }
  size_t FirstOf(const Eigen::Vector3i& chunk) const { return first_of_chunk_.at(chunk); }

 private:
  std::vector<LeafRef> leaves_;
  std::unordered_map<Eigen::Vector3i, size_t, z13::building::primitives::CellHash> first_of_chunk_;
};

}  // namespace z13::station::rooms
