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
#include <unordered_map>

#include <lib_core/math.h>

namespace z13::bullet_module {

namespace {

// mass = 0 -> Bullet treats a body as static (immovable); local inertia is
// then unused, so the zero vector below is inert, not a real value.
constexpr float kStaticMass = 0.f;
const btVector3 kZeroVector(0.f, 0.f, 0.f);

btRigidBody::btRigidBodyConstructionInfo MakeStaticBodyInfo(
    btMotionState& motion_state, btCollisionShape& shape) {
  return btRigidBody::btRigidBodyConstructionInfo(kStaticMass, &motion_state, &shape, kZeroVector);
}

// Bullet's user-data slot is a void*; stash the owning flecs entity id in it
// (RaycastEntity's use case) rather than adding a second lookup table.
void* ToUserPointer(flecs::entity_t entity) {
  return reinterpret_cast<void*>(static_cast<uintptr_t>(entity));
}

flecs::entity_t ToEntity(const void* user_pointer) {
  return static_cast<flecs::entity_t>(reinterpret_cast<uintptr_t>(user_pointer));
}

// Backing Bullet objects for one placed block's rigid body; see RigidBody in
// bullet_components.h for why this lives here and not on the component.
// class, not struct: the constructor establishes cross-pointers between
// shape/motion_state/body that direct field access could invalidate.
class BulletBody {
 public:
  BulletBody(flecs::entity_t entity, const btTransform& transform, float size);

  btRigidBody& Body() { return body_; }

  bool HasTransform(const btTransform& transform) const {
    const btTransform& current = body_.getWorldTransform();
    return (current.getOrigin() - transform.getOrigin()).length() <= z13::math::kEpsilon &&
           current.getRotation().angleShortestPath(transform.getRotation()) <= z13::math::kEpsilon;
  }

 private:
  // Declaration order matters: members are destroyed in reverse, and body_
  // depends on shape_/motion_state_ still being alive to tear down.
  btBoxShape shape_;
  btDefaultMotionState motion_state_;
  btRigidBody body_;
};

BulletBody::BulletBody(flecs::entity_t entity, const btTransform& transform, float size)
    : shape_(btVector3(size / 2.f, size / 2.f, size / 2.f)),
      motion_state_(transform),
      body_(MakeStaticBodyInfo(motion_state_, shape_)) {
  body_.setUserPointer(ToUserPointer(entity));
}

// m_normalWorldOnB points from object B to object A; push the probe out
// along it (or against it, if Bullet handed the probe to us as B).
class PenetrationCallback : public btCollisionWorld::ContactResultCallback {
 public:
  explicit PenetrationCallback(const btCollisionObject& probe) : probe_(probe) {
  }

  btVector3 correction { kZeroVector };

  btScalar addSingleResult(
      btManifoldPoint& point, const btCollisionObjectWrapper* wrap_a, int, int,
      const btCollisionObjectWrapper*, int, int) override {
    if (point.getDistance() >= 0.f) {
      return 0.f;
    }
    const btVector3 push = point.m_normalWorldOnB * -point.getDistance();
    correction += wrap_a->getCollisionObject() == &probe_ ? push : -push;
    return 0.f;
  }

 private:
  const btCollisionObject& probe_;
};

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
      : dispatcher(&collision_config),
        dynamics_world(&dispatcher, &broadphase, &solver, &collision_config) {
  }

  // Declaration order matters: members are destroyed in reverse, and
  // dynamics_world depends on all the others still being alive to tear down.
  btDefaultCollisionConfiguration collision_config;
  btCollisionDispatcher dispatcher;
  btDbvtBroadphase broadphase;
  btSequentialImpulseConstraintSolver solver;
  btDiscreteDynamicsWorld dynamics_world;

  // Node-based storage keeps a body's address (and Bullet's internal
  // cross-pointers into it) stable across insert/erase of other bodies.
  std::unordered_map<flecs::entity_t, BulletBody> bodies;
};

PhysicsWorld::PhysicsWorld() : state_(std::make_shared<State>()) {
}

btDiscreteDynamicsWorld& PhysicsWorld::DynamicsWorld() {
  return state_->dynamics_world;
}

void PhysicsWorld::SyncBody(flecs::entity_t entity, const btTransform& transform, float size) {
  if (const auto existing = state_->bodies.find(entity); existing != state_->bodies.end()) {
    if (existing->second.HasTransform(transform)) {
      return;
    }
    RemoveBody(entity);
  }

  const auto it = state_->bodies.try_emplace(entity, entity, transform, size).first;
  state_->dynamics_world.addRigidBody(&it->second.Body());
}

void PhysicsWorld::RemoveBody(flecs::entity_t entity) {
  auto it = state_->bodies.find(entity);
  if (it == state_->bodies.end()) {
    return;
  }
  state_->dynamics_world.removeRigidBody(&it->second.Body());
  state_->bodies.erase(it);
}

void PhysicsWorld::RemoveBodiesIf(const std::function<bool(flecs::entity_t)>& should_remove) {
  for (auto it = state_->bodies.begin(); it != state_->bodies.end();) {
    if (should_remove(it->first)) {
      state_->dynamics_world.removeRigidBody(&it->second.Body());
      it = state_->bodies.erase(it);
    } else {
      ++it;
    }
  }
}

size_t PhysicsWorld::BodyCount() const {
  return state_->bodies.size();
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
  state_->dynamics_world.contactTest(&probe, callback);
  return desired_center + callback.correction;
}

std::optional<flecs::entity_t> PhysicsWorld::RaycastEntity(const btVector3& from, const btVector3& to) {
  btCollisionWorld::ClosestRayResultCallback callback(from, to);
  state_->dynamics_world.rayTest(from, to, callback);
  if (!callback.hasHit()) {
    return std::nullopt;
  }
  return ToEntity(callback.m_collisionObject->getUserPointer());
}

}  // namespace z13::bullet_module
