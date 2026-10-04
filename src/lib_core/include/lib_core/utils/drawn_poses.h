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

#include <functional>
#include <unordered_map>

#include <Eigen/Dense>
#include <flecs.h>

#include <lib_core/utils/pose_smoothing.h>

namespace z13 {

// Where a renderer draws the entities it smooths, chasing their simulated transforms.
struct DrawnPoses {
  std::unordered_map<flecs::entity_t, math::SmoothedPose> by_entity;
};

// Moves every entity `query` matches one frame towards its transform, except `is_own`
// ones, which are drawn as simulated. Rebuilt each call, so gone entities drop out.
void ChaseDrawnPoses(
    DrawnPoses& drawn, const flecs::query<const Eigen::Matrix4f>& query, float delta_seconds,
    const math::PoseSmoothingParams& params, const std::function<bool(flecs::entity)>& is_own);

// `transform` as drawn: the chased pose for a smoothed entity, as is otherwise.
Eigen::Matrix4f DrawnTransform(const DrawnPoses& drawn, flecs::entity e, const Eigen::Matrix4f& transform);

}  // namespace z13
