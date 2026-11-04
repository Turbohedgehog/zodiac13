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

#include <primitives/block_group.h>

#include <primitives/placement.h>

namespace z13::building::primitives {

namespace {

// PackChunk fills 63 bits, so the top one is free.
constexpr uint64_t kGridGroupFlag = uint64_t {1} << 63;

}  // namespace

uint64_t BlockGroupId(z13::station::GridId grid, const Eigen::Vector3i& chunk) {
  return grid == z13::station::kMainGrid ? PackChunk(chunk) : (kGridGroupFlag | grid);
}

std::optional<z13::station::GridId> GridOfGroup(uint64_t group) {
  return (group & kGridGroupFlag) != 0 ? std::optional(static_cast<z13::station::GridId>(group & ~kGridGroupFlag))
                                       : std::nullopt;
}

}  // namespace z13::building::primitives
