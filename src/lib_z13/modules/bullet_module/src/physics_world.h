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

#include <memory>
#include <optional>

#include <flecs.h>
#include <Eigen/Dense>

#include <bullet/btBulletDynamicsCommon.h>

namespace z13::bullet_module {

btTransform ToBtTransform(const Eigen::Matrix4f& transform);

// Private to this module and never serialized, so exempt from the "no
// pointers in components" rule (see CLAUDE.md).
class PhysicsWorld {
 public:
  PhysicsWorld();

  btDiscreteDynamicsWorld& DynamicsWorld();

  void AddBody(flecs::entity_t entity, const btTransform& transform, float size);
  void RemoveBody(flecs::entity_t entity);

  // Pushes a sphere out of any placed block it penetrates. No persistent
  // body: the player is purely kinematic, so this is a one-off query.
  btVector3 ResolveSpherePosition(const btVector3& desired_center, float radius);

  // Entity id of whichever placed block's body the ray hits first, if any.
  std::optional<flecs::entity_t> RaycastEntity(const btVector3& from, const btVector3& to);

 private:
  class State;

  std::shared_ptr<State> state_;
};

}  // namespace z13::bullet_module
