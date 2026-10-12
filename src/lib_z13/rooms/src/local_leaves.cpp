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


#include "local_leaves.h"

namespace z13::station::rooms {

void LocalLeaves::Add(const LeafRef& ref) {
  if (index_.try_emplace(ref, leaves_.size()).second) {
    leaves_.push_back(ref);
  }
}

std::optional<size_t> LocalLeaves::IndexOf(const LeafRef& ref) const {
  const auto found = index_.find(ref);
  return found == index_.end() ? std::nullopt : std::optional(found->second);
}

}  // namespace z13::station::rooms
