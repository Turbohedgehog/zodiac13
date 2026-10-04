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

#include <lib_core/utils/drawn_poses.h>

#include <utility>

namespace z13 {

void ChaseDrawnPoses(
    DrawnPoses& drawn, const flecs::query<const Eigen::Matrix4f>& query, float delta_seconds,
    const math::PoseSmoothingParams& params, const std::function<bool(flecs::entity)>& is_own) {
  std::unordered_map<flecs::entity_t, math::SmoothedPose> next;
  query.each([&](flecs::entity e, const Eigen::Matrix4f& transform) {
    if (is_own(e)) {
      return;
    }
    const auto previous = drawn.by_entity.find(e.id());
    math::SmoothedPose pose = previous != drawn.by_entity.end() ? previous->second : math::PoseAt(transform);
    math::ChasePose(pose, transform, delta_seconds, params);
    next.emplace(e.id(), pose);
  });
  drawn.by_entity = std::move(next);
}

Eigen::Matrix4f DrawnTransform(const DrawnPoses& drawn, flecs::entity e, const Eigen::Matrix4f& transform) {
  const auto found = drawn.by_entity.find(e.id());
  return found != drawn.by_entity.end() ? math::DrawnTransform(found->second, transform) : transform;
}

}  // namespace z13
