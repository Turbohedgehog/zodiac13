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


#include "door_leaf_mesh.h"

#include <algorithm>
#include <utility>

#include <z13/components/door.h>
#include <primitives/chunk_mesh.h>

namespace z13::door_render {

std::optional<z13::render::SceneMesh> BuildLeafMesh(
    const z13::station::Block& block, int position, z13::building::primitives::OptionalPalette palette) {
  z13::building::primitives::Mesh leaf = z13::building::primitives::DoorLeafMesh(block, palette);
  const float open = static_cast<float>(std::clamp(position, 0, z13::station::kDoorOpen)) /
                     static_cast<float>(z13::station::kDoorOpen);
  if (leaf.positions.empty() || open >= 1.f) {
    return std::nullopt;
  }
  const auto highest = std::ranges::max_element(
      leaf.positions, {}, [](const Eigen::Vector3f& position) { return position.z(); });
  const float top = highest->z();
  const float closed = 1.f - open;
  std::ranges::for_each(leaf.positions, [top, closed](Eigen::Vector3f& position) {
    position.z() = top - ((top - position.z()) * closed);
    position *= z13::station::kCellSize;
  });
  const z13::Rgba color = z13::building::primitives::MaterialOf(palette, block.spec.type_id).first;
  return z13::render::SceneMesh {
      .positions = std::move(leaf.positions), .normals = std::move(leaf.normals), .color = color};
}

}  // namespace z13::door_render
