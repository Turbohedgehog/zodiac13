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

#include <z13_primitives/draw_order.h>

#include <algorithm>
#include <iterator>
#include <utility>

#include <z13_primitives/palette.h>
#include <z13_primitives/placement.h>

namespace z13::building::primitives {

namespace {

float SquaredDistance(const z13::station::Block& block, const Eigen::Vector3f& eye) {
  const CellBox box = OccupiedCells(block);
  const Eigen::Vector3f centre = box.min.cast<float>() + box.extent.cast<float>() / 2.f;
  return (centre - eye).squaredNorm();
}

}  // namespace

bool IsTransparent(const z13::station::Block& block, OptionalPalette palette) {
  const auto primitive = palette ? palette->get().Find(block.spec.type_id) : std::nullopt;
  return primitive && primitive->get().Has(PrimitiveFlags::Transparent);
}

DrawOrder SortForDrawing(
    std::span<const z13::station::Block> blocks, OptionalPalette palette, const Eigen::Vector3f& eye) {
  DrawOrder order;
  std::vector<std::pair<float, size_t>> transparent;
  for (size_t i = 0; i < blocks.size(); ++i) {
    if (IsTransparent(blocks[i], palette)) {
      transparent.emplace_back(SquaredDistance(blocks[i], eye), i);
    } else {
      order.opaque.push_back(i);
    }
  }
  std::ranges::sort(transparent, std::ranges::greater {});
  order.transparent.reserve(transparent.size());
  std::ranges::transform(transparent, std::back_inserter(order.transparent), &std::pair<float, size_t>::second);
  return order;
}

}  // namespace z13::building::primitives
