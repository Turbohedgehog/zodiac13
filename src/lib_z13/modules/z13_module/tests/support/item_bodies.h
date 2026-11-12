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

#include <Eigen/Dense>
#include <flecs.h>

#include <lib_core/state/world_state.h>

// What a dynamic item will carry in the state (docs/dynamic-objects-plan.md): a pose and both velocities, and a tag
// on the ones that move. The benchmarks and the cross-platform dump share them.
namespace z13::testing::bench {

struct ItemBody {
  using State = void;
  Eigen::Vector3f position = Eigen::Vector3f::Zero();
  Eigen::Vector3f orientation_vector = Eigen::Vector3f::Zero();
  float orientation_w = 1.f;
  Eigen::Vector3f linear_velocity = Eigen::Vector3f::Zero();
  Eigen::Vector3f angular_velocity = Eigen::Vector3f::Zero();
};

struct ItemAwake {
  using State = void;
};

// Registers both as state and the system that moves the awake items by their linear velocity.
inline void RegisterItemBodies(flecs::world& world, float delta_seconds) {
  z13::flecs_tools::RegisterComponents<ItemBody, ItemAwake>(world);
  world.system<ItemBody>("BenchItems::Move").with<ItemAwake>().each([delta_seconds](ItemBody& body) {
    body.position += body.linear_velocity * delta_seconds;
  });
}

}  // namespace z13::testing::bench
