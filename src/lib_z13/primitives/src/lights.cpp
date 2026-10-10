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

#include <primitives/lights.h>

#include <primitives/placement.h>

namespace z13::building::primitives {

namespace {

constexpr float kBelowFaceCells = 0.5f;

}  // namespace

std::vector<PointLight> LightsOf(std::span<const z13::station::Block> blocks, OptionalPalette palette) {
  std::vector<PointLight> lights;
  if (!palette) {
    return lights;
  }
  for (const z13::station::Block& block : blocks) {
    const auto primitive = palette->get().Find(block.spec.type_id);
    if (!primitive || !primitive->get().light) {
      continue;
    }
    const CellPose pose = PoseOf(block);
    const Eigen::Vector3f below_centre(block.spec.size.x() / 2.f, block.spec.size.y() / 2.f, -kBelowFaceCells);
    const Eigen::Vector3f position = pose.origin.cast<float>() + pose.rotation.cast<float>() * below_centre;
    lights.push_back({
        .position = position,
        .cell = position.array().floor().cast<int>(),
        .source = *primitive->get().light,
    });
  }
  return lights;
}

}  // namespace z13::building::primitives
