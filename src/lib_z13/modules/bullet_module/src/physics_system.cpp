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

#include "physics_system.h"

#include "physics_world.h"

#include <flecs.h>
#include <Eigen/Dense>

#include <bullet/btBulletDynamicsCommon.h>

#include <bullet_module/bullet_components.h>

#include <lib_core/components.h>
#include <lib_core/math.h>

#include <z13/components/building.h>
#include <z13/components/gameplay.h>

namespace z13::bullet_module {

namespace {

constexpr int kMaxSubSteps = 10;

struct PhysicsStepPhase {};
struct DestroyBlockPhase {};

// How far along the player's forward axis DestroyBlock's raycast reaches.
constexpr float kDestroyReachDistance = 5.f;

void RegisterPipeline(flecs::world world) {
  world.component<PhysicsStepPhase>().add(flecs::Phase).depends_on<z13::gameplay::PreUpdatePhase>();
  // Runs after movement/brush updates so DestroyBlock's raycast sees this
  // frame's final player transform.
  world.component<DestroyBlockPhase>().add(flecs::Phase).depends_on<z13::gameplay::PostUpdatePhase>();
}

void RegisterComponents(flecs::world world) {
  world.component<PhysicsWorld>().add(flecs::Singleton);
  world.component<RigidBody>();
}

// Only term is a singleton, so $this is empty and the entity-taking .each()
// overload would assert; use the iter/row overload.
void StepPhysicsWorld(flecs::iter& it, size_t, PhysicsWorld& physics_world) {
  physics_world.DynamicsWorld().stepSimulation(it.delta_time(), kMaxSubSteps);
}

void OnBasicBlockAdded(
    flecs::entity e, const z13::building::BasicBlock&, const Eigen::Matrix4f& transform,
    PhysicsWorld& physics_world) {
  physics_world.AddBody(e.id(), ToBtTransform(transform), z13::building::kBlockSize);
  e.add<RigidBody>();
}

void OnRigidBodyRemoved(flecs::entity e, RigidBody, PhysicsWorld& physics_world) {
  physics_world.RemoveBody(e.id());
}

// Mutate-then-e.set() idiom (like BuildingSystem::UpdateBrush), so other OnSet
// observers see the correction; IsNear stops it retriggering once resolved.
void OnPlayerMoved(
    flecs::entity e, const z13::gameplay::PlayerCollider& collider, Eigen::Matrix4f& transform,
    PhysicsWorld& physics_world) {
  const Eigen::Vector3f position = z13::math::ExtractTranslation<float>(transform);
  const btVector3 resolved = physics_world.ResolveSpherePosition(
      btVector3(position.x(), position.y(), position.z()), collider.radius);
  const Eigen::Vector3f resolved_position(resolved.x(), resolved.y(), resolved.z());
  if (z13::math::IsNear(position, resolved_position)) {
    return;
  }

  z13::math::SetTranslation(resolved_position, transform);
  e.set(transform);
}

void ProcessDestroyBlockRequest(
    flecs::entity player, z13::building::RequestDestroyBlock, const Eigen::Matrix4f& transform,
    PhysicsWorld& physics_world) {
  player.remove<z13::building::RequestDestroyBlock>();

  const Eigen::Vector3f origin = z13::math::ExtractTranslation<float>(transform);
  const Eigen::Vector3f forward = transform.block<3, 3>(0, 0).col(0);
  const Eigen::Vector3f target = origin + forward * kDestroyReachDistance;

  const auto hit = physics_world.RaycastEntity(
      btVector3(origin.x(), origin.y(), origin.z()), btVector3(target.x(), target.y(), target.z()));
  if (hit) {
    player.world().entity(*hit).destruct();
  }
}

void RegisterSystems(flecs::world world) {
  // Set here, not next to `.add(flecs::Singleton)`: doing both in one event aborts
  // with flecs_assert_relation_unused (same split as raylib_module's Lighting).
  world.set<PhysicsWorld>(PhysicsWorld());

  world.system<PhysicsWorld>("PhysicsSystem::StepPhysicsWorld")
      .kind<PhysicsStepPhase>()
      .each(StepPhysicsWorld);

  world.observer<const z13::building::BasicBlock, const Eigen::Matrix4f, PhysicsWorld>(
           "PhysicsSystem::OnBasicBlockAdded")
      .event(flecs::OnAdd)
      .without<RigidBody>()
      .write<RigidBody>()
      .yield_existing()
      .each(OnBasicBlockAdded);

  world.observer<RigidBody, PhysicsWorld>("PhysicsSystem::OnRigidBodyRemoved")
      .event(flecs::OnRemove)
      .each(OnRigidBodyRemoved);

  world.observer<const z13::gameplay::PlayerCollider, Eigen::Matrix4f, PhysicsWorld>(
           "PhysicsSystem::OnPlayerMoved")
      .event(flecs::OnSet)
      .each(OnPlayerMoved);

  world.system<z13::building::RequestDestroyBlock, const Eigen::Matrix4f, PhysicsWorld>(
           "PhysicsSystem::ProcessDestroyBlockRequest")
      .kind<DestroyBlockPhase>()
      .each(ProcessDestroyBlockRequest);
}

}  // namespace

void PhysicsSystem::Register(flecs::world& world) {
  world.observer<RegisterComponentsEvent>("PhysicsSystem::RegisterComponents")
      .event(flecs::OnAdd)
      .yield_existing()
      .each([world = world](const auto&) { RegisterComponents(world); });

  world.observer<InitPhasesEvent>("PhysicsSystem::RegisterPipeline")
      .event(flecs::OnAdd)
      .yield_existing()
      .each([world = world](const auto&) { RegisterPipeline(world); });

  world.observer<InitSystemsEvent>("PhysicsSystem::RegisterSystems")
      .event(flecs::OnAdd)
      .yield_existing()
      .each([world = world](const auto&) { RegisterSystems(world); });
}

}  // namespace z13::bullet_module
