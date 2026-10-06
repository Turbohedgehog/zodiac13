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

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include <flecs.h>
#include <Eigen/Dense>

#include <bullet/btBulletDynamicsCommon.h>

#include <z13_primitives/geometry.h>

namespace z13::bullet_module {

btTransform ToBtTransform(const Eigen::Matrix4f& transform);

using z13::building::primitives::BlockShapeKey;

// The shape's convex pieces in meters, in the body's frame; called only when no body
// uses the shape yet.
using SolidsBuilder = std::function<std::vector<z13::building::primitives::ConvexSolid>()>;

// Private to this module and never serialized, so exempt from the "no
// pointers in components" rule (see CLAUDE.md).
class PhysicsWorld {
 public:
  using Singleton = void;

  PhysicsWorld();

  btDiscreteDynamicsWorld& DynamicsWorld();

  // Creates the entity's body, or recreates it if its shape or `transform` no longer match.
  void SyncBody(
      flecs::entity_t entity, const BlockShapeKey& shape, const btTransform& transform, const SolidsBuilder& solids);
  void RemoveBody(flecs::entity_t entity);

  // Removes every body whose entity `should_remove` accepts; no per-call allocation.
  void RemoveBodiesIf(const std::function<bool(flecs::entity_t)>& should_remove);

  size_t BodyCount() const;

  // Moves a sphere from `from` toward `to`, stopping at placed blocks and sliding along
  // them (at most `max_iterations` slides), so a long step can't pass through a block.
  btVector3 SweepSphere(const btVector3& from, const btVector3& to, float radius, uint32_t max_iterations);

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
