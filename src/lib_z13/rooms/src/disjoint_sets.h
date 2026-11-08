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
#include <vector>

namespace z13::station::rooms {

// Union-find over 0..count-1: the connected sets of leaves, without a graph to build and tear down.
class DisjointSets {
 public:
  explicit DisjointSets(size_t count);

  void Join(size_t a, size_t b);
  // Per element, the representative of its set; equal for elements of one set.
  std::vector<size_t> Labels();

 private:
  size_t Find(size_t element);

  std::vector<size_t> parent_;
};

}  // namespace z13::station::rooms
