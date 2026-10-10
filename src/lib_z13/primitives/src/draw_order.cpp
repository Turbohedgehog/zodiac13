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

#include <primitives/draw_order.h>

#include <algorithm>
#include <numeric>

#include <primitives/palette.h>

namespace z13::building::primitives {

bool IsTransparent(const z13::station::Block& block, OptionalPalette palette) {
  const auto primitive = palette ? palette->get().Find(block.spec.type_id) : std::nullopt;
  return primitive && primitive->get().Has(PrimitiveFlags::Transparent);
}

std::vector<size_t> FarthestFirst(std::span<const ChunkBounds> chunks, const Eigen::Vector3f& eye) {
  std::vector<float> distances(chunks.size());
  std::ranges::transform(chunks, distances.begin(), [&eye](const ChunkBounds& chunk) {
    return (chunk.bounds.center() - eye).squaredNorm();
  });
  std::vector<size_t> order(chunks.size());
  std::iota(order.begin(), order.end(), size_t {});
  std::ranges::sort(order, [&chunks, &distances](size_t a, size_t b) {
    if (distances[a] != distances[b]) {
      return distances[a] > distances[b];
    }
    const Eigen::Vector3i& key_a = chunks[a].key;
    const Eigen::Vector3i& key_b = chunks[b].key;
    return std::lexicographical_compare(key_a.data(), key_a.data() + key_a.size(), key_b.data(),
                                        key_b.data() + key_b.size());
  });
  return order;
}

}  // namespace z13::building::primitives
