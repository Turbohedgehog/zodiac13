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

#include <lib_core/state/world_state.h>
#include <lib_core/utils/flecs_utils.h>
#include <lib_core/utils/math.h>
#include <lib_core/world/components.h>
#include <lib_core/world/lifecycle.h>

#include <z13/components/building.h>
#include <z13/components/gameplay.h>
#include <z13/components/input.h>
#include <z13_settings/physics_tuning.h>

namespace z13::bullet_module {

// Outside the anonymous namespace: its path breaks phase-order ties (see phase_order.h).
struct PhysicsStepPhase {};

// Where the player stood when the frame began; the collision sweep starts there. Derived
// every frame, so a rollback or teleport between frames is never swept.
struct SweepOrigin {
  Eigen::Vector3f position = Eigen::Vector3f::Zero();
};

namespace {

constexpr int kMaxSubSteps = 10;

// How far along the player's forward axis DestroyBlock's raycast reaches.
constexpr float kDestroyReachDistance = 5.f;

void RegisterPipeline(flecs::world world) {
  world.component<PhysicsStepPhase>().add(flecs::Phase).depends_on<z13::gameplay::PreUpdatePhase>();
}

void RegisterComponents(flecs::world world) {
  z13::flecs_tools::RegisterComponents<PhysicsWorld, RigidBody, SweepOrigin, z13::PhysicsTuning>(world);
}

// Only term is a singleton, so $this is empty and the entity-taking .each()
// overload would assert; use the iter/row overload.
void StepPhysicsWorld(flecs::iter& it, size_t, PhysicsWorld& physics_world) {
  physics_world.DynamicsWorld().stepSimulation(it.delta_time(), kMaxSubSteps);
}

// Bodies are derived from block components each frame, not from add/remove events,
// so state restored or edited in place is picked up too.
void SyncBlockBody(
    flecs::entity e, const z13::building::BasicBlock&, const Eigen::Matrix4f& transform,
    PhysicsWorld& physics_world) {
  physics_world.SyncBody(e.id(), ToBtTransform(transform), z13::building::kBlockSize);
  if (!e.has<RigidBody>()) {
    e.add<RigidBody>();
  }
}

void ReleaseOrphanBodies(flecs::iter& it, size_t, PhysicsWorld& physics_world) {
  const flecs::world world = it.world();
  physics_world.RemoveBodiesIf([&world](flecs::entity_t id) {
    if (!world.is_alive(id)) {
      return true;
    }

    const flecs::entity e = world.entity(id);
    if (e.has<z13::building::BasicBlock>()) {
      return false;
    }
    e.remove<RigidBody>();
    return true;
  });
}

btVector3 ToBtVector(const Eigen::Vector3f& vector) {
  return {vector.x(), vector.y(), vector.z()};
}

void RecordSweepOrigin(flecs::entity e, const z13::gameplay::PlayerCollider&, const Eigen::Matrix4f& transform) {
  e.set(SweepOrigin {.position = z13::math::ExtractTranslation<float>(transform)});
}

// Mutate-then-e.set() idiom (like BuildingSystem::UpdateBrush), so OnSet
// observers see the correction. Compared exactly: a sweep can stop a step short by
// less than any epsilon. The push-out still runs after the sweep, for blocks placed
// onto the player.
void ResolvePlayerCollision(
    flecs::entity e, const z13::gameplay::PlayerCollider& collider, const SweepOrigin& origin,
    Eigen::Matrix4f& transform, PhysicsWorld& physics_world, const z13::PhysicsTuning& tuning) {
  const Eigen::Vector3f position = z13::math::ExtractTranslation<float>(transform);
  const btVector3 swept = physics_world.SweepSphere(
      ToBtVector(origin.position), ToBtVector(position), collider.radius, tuning.max_sweep_iterations);
  const btVector3 resolved = physics_world.ResolveSpherePosition(swept, collider.radius);
  const Eigen::Vector3f resolved_position(resolved.x(), resolved.y(), resolved.z());
  if (resolved_position == position) {
    return;
  }

  z13::math::SetTranslation(resolved_position, transform);
  e.set(transform);
}

void ProcessDestroyBlockRequest(
    flecs::entity player, z13::building::RequestDestroyBlock, const z13::gameplay::Player&,
    const Eigen::Matrix4f& transform, PhysicsWorld& physics_world) {
  player.remove<z13::building::RequestDestroyBlock>();

  const Eigen::Vector3f origin = z13::math::ExtractTranslation<float>(transform);
  const Eigen::Vector3f forward = transform.block<3, 3>(0, 0).col(0);
  const Eigen::Vector3f target = origin + forward * kDestroyReachDistance;

  const auto hit = physics_world.RaycastEntity(
      btVector3(origin.x(), origin.y(), origin.z()), btVector3(target.x(), target.y(), target.z()));
  if (hit) {
    physics_world.RemoveBody(*hit);
    player.world().entity(*hit).destruct();
  }
}

// PhysicsWorld exists exactly while a gameplay scene does. It's set here, not next to
// `.add(flecs::Singleton)`: doing both in one event aborts with flecs_assert_relation_unused.
void ReconcilePhysicsWorld(flecs::world world) {
  const bool scene_exists = world.has<z13::gameplay::Gameplay>();
  const bool has_physics_world = world.has<PhysicsWorld>();
  if (scene_exists && !has_physics_world) {
    world.set<PhysicsWorld>(PhysicsWorld());
  } else if (!scene_exists && has_physics_world) {
    world.remove<PhysicsWorld>();
  }
}

void RegisterSystems(flecs::world world) {
  world.system("PhysicsSystem::ReconcilePhysicsWorld")
      .kind<z13::gameplay::PreUpdatePhase>()
      .immediate()
      .each([world]() { ReconcilePhysicsWorld(world); });

  world.system<PhysicsWorld>("PhysicsSystem::StepPhysicsWorld")
      .kind<PhysicsStepPhase>()
      .each(StepPhysicsWorld);

  // PostUpdate runs after the building phase, so a block placed this frame gets its
  // body this frame; systems in one phase run in registration order.
  world.system<PhysicsWorld>("PhysicsSystem::ReleaseOrphanBodies")
      .kind<z13::gameplay::PostUpdatePhase>()
      .read<z13::building::BasicBlock>()
      .write<RigidBody>()
      .each(ReleaseOrphanBodies);

  world.system<const z13::building::BasicBlock, const Eigen::Matrix4f, PhysicsWorld>(
           "PhysicsSystem::SyncBlockBody")
      .kind<z13::gameplay::PostUpdatePhase>()
      .write<RigidBody>()
      .each(SyncBlockBody);

  // Before movement (ApplyActionFramePhase); nothing earlier in the frame moves the player.
  world.system<const z13::gameplay::PlayerCollider, const Eigen::Matrix4f>("PhysicsSystem::RecordSweepOrigin")
      .kind<z13::input::ClearActionFramePhase>()
      .write<SweepOrigin>()
      .each(RecordSweepOrigin);

  world.system<const z13::gameplay::PlayerCollider, const SweepOrigin, Eigen::Matrix4f, PhysicsWorld,
               const z13::PhysicsTuning>("PhysicsSystem::ResolvePlayerCollision")
      .kind<z13::gameplay::PostUpdatePhase>()
      .each(ResolvePlayerCollision);

  // After ResolvePlayerCollision in the same phase, so the raycast sees the final player
  // transform; a later phase would run after rendering. write<> merges the destroy early.
  world.system<z13::building::RequestDestroyBlock, const z13::gameplay::Player, const Eigen::Matrix4f, PhysicsWorld>(
           "PhysicsSystem::ProcessDestroyBlockRequest")
      .kind<z13::gameplay::PostUpdatePhase>()
      .order_by<z13::gameplay::Player>(z13::gameplay::CompareByPlayerId)
      .write<z13::building::BasicBlock>()
      .each(ProcessDestroyBlockRequest);
}

}  // namespace

void PhysicsSystem::Register(flecs::world& world) {
  OnRegisterComponents(world, RegisterComponents);

  OnInitPhases(world, RegisterPipeline);

  OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::bullet_module
