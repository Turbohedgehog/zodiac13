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


#include "disjoint_sets.h"

#include <algorithm>
#include <numeric>
#include <utility>

namespace z13::station::rooms {

DisjointSets::DisjointSets(size_t count) : parent_(count) {
  std::iota(parent_.begin(), parent_.end(), size_t {});
}

size_t DisjointSets::Find(size_t element) {
  while (parent_[element] != element) {
    parent_[element] = parent_[parent_[element]];
    element = parent_[element];
  }
  return element;
}

void DisjointSets::Join(size_t a, size_t b) {
  const size_t root_a = Find(a);
  const size_t root_b = Find(b);
  if (root_a != root_b) {
    parent_[std::max(root_a, root_b)] = std::min(root_a, root_b);
  }
}

std::vector<size_t> DisjointSets::Labels() {
  std::vector<size_t> labels(parent_.size());
  for (size_t i = 0; i < labels.size(); ++i) {
    labels[i] = Find(i);
  }
  return labels;
}

}  // namespace z13::station::rooms
