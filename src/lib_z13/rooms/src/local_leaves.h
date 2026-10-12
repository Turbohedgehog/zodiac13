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
#include <optional>
#include <unordered_map>
#include <vector>

#include <rooms/free_space.h>

namespace z13::station::rooms {

// The leaves whose rooms an update finds again, numbered in the order they were added.
class LocalLeaves {
 public:
  void Add(const LeafRef& ref);
  bool Contains(const LeafRef& ref) const { return index_.contains(ref); }
  std::optional<size_t> IndexOf(const LeafRef& ref) const;

  const std::vector<LeafRef>& Leaves() const { return leaves_; }

 private:
  std::vector<LeafRef> leaves_;
  std::unordered_map<LeafRef, size_t, LeafRefHash> index_;
};

}  // namespace z13::station::rooms
