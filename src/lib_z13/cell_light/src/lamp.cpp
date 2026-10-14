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

#include <cell_light/lamp.h>

namespace z13::station::light {

namespace {

constexpr float kChannelMax = 255.f;

}  // namespace

Lamp LampOf(const z13::building::primitives::PointLight& light) {
  const auto& color = light.source.color;
  return {
      .position = light.position,
      .behind = light.behind,
      .color = Eigen::Vector3f(color[0], color[1], color[2]) * (light.source.intensity / kChannelMax),
      .radius = static_cast<float>(light.source.radius_cells),
  };
}

}  // namespace z13::station::light
