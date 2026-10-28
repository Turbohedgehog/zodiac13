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

#include "physics_world.h"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <utility>

#include <lib_core/utils/math.h>

#include "block_shape.h"
#include "blocking_sweep_callback.h"
#include "closest_ray_callback.h"
#include "penetration_callback.h"

namespace z13::bullet_module {

namespace {

// Stops this short of a hit surface, so the next step doesn't start touching it.
constexpr float kSweepSkin = 1e-3f;
constexpr float kMinSweepDistance = 1e-5f;

// Bullet's user-data slot is a void*; stash the owning flecs entity id in it
// (RaycastEntity's use case) rather than adding a second lookup table.
void* ToUserPointer(flecs::entity_t entity) {
  return reinterpret_cast<void*>(static_cast<uintptr_t>(entity));
}

flecs::entity_t ToEntity(const void* user_pointer) {
  return static_cast<flecs::entity_t>(reinterpret_cast<uintptr_t>(user_pointer));
}

// Backing Bullet object for one placed block's body; see RigidBody in
// bullet_components.h for why this lives here and not on the component.
// class, not struct: the constructor establishes cross-pointers between
// shape/body that direct field access could invalidate.
class BulletBody {
 public:
  BulletBody(
      flecs::entity_t entity, const btTransform& transform, BlockShapeKey key, std::shared_ptr<BlockShape> shape);

  btCollisionObject& Body() { return body_; }

  bool Matches(const BlockShapeKey& key, const btTransform& transform) const {
    const btTransform& current = body_.getWorldTransform();
    return key == key_ && (current.getOrigin() - transform.getOrigin()).length() <= z13::math::kEpsilon &&
           current.getRotation().angleShortestPath(transform.getRotation()) <= z13::math::kEpsilon;
  }

 private:
  // Declaration order matters: members are destroyed in reverse, and body_
  // depends on shape_ still being alive to tear down.
  BlockShapeKey key_;
  std::shared_ptr<BlockShape> shape_;
  btCollisionObject body_;
};

BulletBody::BulletBody(
    flecs::entity_t entity, const btTransform& transform, BlockShapeKey key, std::shared_ptr<BlockShape> shape)
    : key_(std::move(key)),
      shape_(std::move(shape)) {
  body_.setCollisionShape(&shape_->Shape());
  body_.setWorldTransform(transform);
  body_.setCollisionFlags(btCollisionObject::CF_STATIC_OBJECT);
  body_.setUserPointer(ToUserPointer(entity));
}

btTransform AtPosition(const btVector3& position) {
  btTransform transform;
  transform.setIdentity();
  transform.setOrigin(position);
  return transform;
}

}  // namespace

btTransform ToBtTransform(const Eigen::Matrix4f& transform) {
  const Eigen::Vector3f position = z13::math::ExtractTranslation<float>(transform);
  const Eigen::Quaternionf rotation = z13::math::ExtractQuat<float>(transform);

  btTransform bt_transform;
  bt_transform.setIdentity();
  bt_transform.setOrigin(btVector3(position.x(), position.y(), position.z()));
  bt_transform.setRotation(btQuaternion(rotation.x(), rotation.y(), rotation.z(), rotation.w()));
  return bt_transform;
}

// class, not struct: the constructor wires up cross-pointers between
// members (see field comment below) that direct field access could break.
class PhysicsWorld::State {
 public:
  State()
      : dispatcher_(&collision_config_),
        world_(&dispatcher_, &broadphase_, &collision_config_) {}

  btCollisionWorld& World() { return world_; }

  size_t BodyCount() const { return bodies_.size(); }

  void SyncBody(
      flecs::entity_t entity, const BlockShapeKey& key, const btTransform& transform, const SolidsBuilder& solids) {
    if (const auto existing = bodies_.find(entity); existing != bodies_.end()) {
      if (existing->second.Matches(key, transform)) {
        return;
      }
      RemoveBody(entity);
    }

    const auto it = bodies_.try_emplace(entity, entity, transform, key, SharedShape(key, solids)).first;
    world_.addCollisionObject(&it->second.Body());
  }

  void RemoveBody(flecs::entity_t entity) {
    if (const auto it = bodies_.find(entity); it != bodies_.end()) {
      world_.removeCollisionObject(&it->second.Body());
      bodies_.erase(it);
    }
  }

  void RemoveBodiesIf(const std::function<bool(flecs::entity_t)>& should_remove) {
    for (auto it = bodies_.begin(); it != bodies_.end();) {
      if (should_remove(it->first)) {
        world_.removeCollisionObject(&it->second.Body());
        it = bodies_.erase(it);
      } else {
        ++it;
      }
    }
  }

 private:
  std::shared_ptr<BlockShape> SharedShape(const BlockShapeKey& key, const SolidsBuilder& solids) {
    std::weak_ptr<BlockShape>& cached = shapes_[key];
    if (auto shape = cached.lock()) {
      return shape;
    }
    auto shape = std::make_shared<BlockShape>(solids());
    cached = shape;
    return shape;
  }

  // Declaration order matters: members are destroyed in reverse, so
  // world_ must come after everything it points into (its own
  // collaborators and bodies_) to still find them alive while tearing down.
  btDefaultCollisionConfiguration collision_config_;
  btCollisionDispatcher dispatcher_;
  btDbvtBroadphase broadphase_;

  // Node-based storage keeps a body's address (and Bullet's internal
  // cross-pointers into it) stable across insert/erase of other bodies.
  std::unordered_map<flecs::entity_t, BulletBody> bodies_;
  // Weak: a shape goes with the last body using it.
  std::unordered_map<BlockShapeKey, std::weak_ptr<BlockShape>, z13::building::primitives::BlockShapeKeyHash> shapes_;

  btCollisionWorld world_;
};

PhysicsWorld::PhysicsWorld() : state_(std::make_shared<State>()) {
}

btCollisionWorld& PhysicsWorld::World() {
  return state_->World();
}

void PhysicsWorld::SyncBody(
    flecs::entity_t entity, const BlockShapeKey& shape, const btTransform& transform, const SolidsBuilder& solids) {
  state_->SyncBody(entity, shape, transform, solids);
}

void PhysicsWorld::RemoveBody(flecs::entity_t entity) {
  state_->RemoveBody(entity);
}

void PhysicsWorld::RemoveBodiesIf(const std::function<bool(flecs::entity_t)>& should_remove) {
  state_->RemoveBodiesIf(should_remove);
}

size_t PhysicsWorld::BodyCount() const {
  return state_->BodyCount();
}

btVector3 PhysicsWorld::ResolveSpherePosition(const btVector3& desired_center, float radius) {
  btSphereShape probe_shape(radius);
  btCollisionObject probe;
  probe.setCollisionShape(&probe_shape);
  btTransform probe_transform;
  probe_transform.setIdentity();
  probe_transform.setOrigin(desired_center);
  probe.setWorldTransform(probe_transform);

  PenetrationCallback callback(probe);
  state_->World().contactTest(&probe, callback);
  return desired_center + callback.Correction();
}

btVector3 PhysicsWorld::SweepSphere(const btVector3& from, const btVector3& to, float radius, uint32_t max_iterations) {
  const btSphereShape shape(radius);
  btVector3 position = from;
  btVector3 remaining = to - from;
  for (uint32_t i = 0; i < max_iterations && remaining.length() > kMinSweepDistance; ++i) {
    // Exactly `to` when unobstructed, so an unblocked step leaves the position bit-identical.
    const btVector3 target = i == 0 ? to : position + remaining;
    BlockingSweepCallback callback(target - position);
    state_->World().convexSweepTest(&shape, AtPosition(position), AtPosition(target), callback);
    if (!callback.Hit()) {
      return target;
    }

    const auto& [fraction, normal] = *callback.Hit();
    position += remaining * fraction + normal * kSweepSkin;
    remaining *= 1.f - fraction;
    remaining -= normal * normal.dot(remaining);
  }
  return position;
}

std::optional<flecs::entity_t> PhysicsWorld::RaycastEntity(const btVector3& from, const btVector3& to) {
  ClosestRayCallback callback;
  state_->World().rayTest(from, to, callback);
  if (!callback.Hit()) {
    return std::nullopt;
  }
  return ToEntity(callback.Hit()->object.get().getUserPointer());
}

}  // namespace z13::bullet_module
