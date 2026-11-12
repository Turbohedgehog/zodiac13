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

#include <cstdint>

#include <Eigen/Dense>

#include <z13/components/station/block_spec.h>

namespace z13::kinematic {

// A solid body whose pose is a function of the tick (docs/dynamic-objects-plan.md, kind 4): it goes straight from
// `from` to `to` over `duration_ticks` from `start_tick` and stays at either end. Positions are the meters the
// body's lowest corner stands at; a player on it is carried along (bullet_module).
struct KinematicBody {
  using State = void;
  z13::station::BlockSpec spec;
  Eigen::Vector3f from = Eigen::Vector3f::Zero();
  Eigen::Vector3f to = Eigen::Vector3f::Zero();
  uint64_t start_tick {};
  uint32_t duration_ticks {};
};

inline Eigen::Vector3f PositionAt(const KinematicBody& body, uint64_t tick) {
  if (tick <= body.start_tick) {
    return body.from;
  }
  const uint64_t elapsed = tick - body.start_tick;
  if (elapsed >= body.duration_ticks) {
    return body.to;
  }
  const float progress = static_cast<float>(elapsed) / static_cast<float>(body.duration_ticks);
  return body.from + ((body.to - body.from) * progress);
}

}  // namespace z13::kinematic
