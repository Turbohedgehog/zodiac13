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

#include <rooms/disjoint_sets.h>

#include <numeric>

namespace z13::station::rooms {

DisjointSets::DisjointSets(size_t size) : parent_(size) {
  std::iota(parent_.begin(), parent_.end(), size_t {});
}

size_t DisjointSets::Find(size_t item) {
  while (parent_[item] != item) {
    parent_[item] = parent_[parent_[item]];
    item = parent_[item];
  }
  return item;
}

void DisjointSets::Union(size_t first, size_t second) {
  parent_[Find(first)] = Find(second);
}

}  // namespace z13::station::rooms
