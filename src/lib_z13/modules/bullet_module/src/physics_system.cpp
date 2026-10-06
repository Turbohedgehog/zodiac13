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

#include <functional>
#include <optional>
#include <vector>

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
#include <z13/components/station.h>
#include <z13_primitives/geometry.h>
#include <z13_primitives/palette.h>
#include <z13_primitives/placement.h>
#include <z13_settings/building_tuning.h>
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

void RegisterPipeline(flecs::world world) {
  world.component<PhysicsStepPhase>().add(flecs::Phase).depends_on<z13::gameplay::PreUpdatePhase>();
}

void RegisterComponents(flecs::world world) {
  z13::flecs_tools::RegisterComponents<PhysicsWorld, RigidBody, SweepOrigin, z13::PhysicsTuning, z13::BuildingTuning>(
      world);
}

// Only term is a singleton, so $this is empty and the entity-taking .each()
// overload would assert; use the iter/row overload.
void StepPhysicsWorld(flecs::iter& it, size_t, PhysicsWorld& physics_world) {
  physics_world.DynamicsWorld().stepSimulation(it.delta_time(), kMaxSubSteps);
}

using BlockQuery = flecs::query<const z13::station::Block>;
using z13::building::primitives::BlockPalette;
using z13::building::primitives::OptionalPalette;

btTransform BlockTransform(const z13::station::Block& block) {
  const Eigen::Isometry3f pose =
      z13::building::primitives::WorldPose(z13::building::primitives::PoseOf(block), z13::station::kCellSize);
  const Eigen::Matrix3f& r = pose.linear();
  btTransform transform;
  transform.setBasis(btMatrix3x3(r(0, 0), r(0, 1), r(0, 2), r(1, 0), r(1, 1), r(1, 2), r(2, 0), r(2, 1), r(2, 2)));
  transform.setOrigin(btVector3(pose.translation().x(), pose.translation().y(), pose.translation().z()));
  return transform;
}

void SyncBlockBody(
    flecs::entity e, const z13::station::Block& block, PhysicsWorld& physics_world, OptionalPalette palette) {
  const BlockShapeKey shape {.type_id = block.spec.type_id, .size = block.spec.size};
  physics_world.SyncBody(e.id(), shape, BlockTransform(block), [&shape, palette]() {
    return z13::building::primitives::Scaled(
        z13::building::primitives::BlockSolids(palette, shape), z13::station::kCellSize);
  });
  if (!e.has<RigidBody>()) {
    e.add<RigidBody>();
  }
}

void ReleaseOrphanBodies(const flecs::world& world, PhysicsWorld& physics_world) {
  physics_world.RemoveBodiesIf([&world](flecs::entity_t id) {
    if (!world.is_alive(id)) {
      return true;
    }

    const flecs::entity e = world.entity(id);
    if (e.has<z13::station::Block>()) {
      return false;
    }
    e.remove<RigidBody>();
    return true;
  });
}

void SyncBlockTables(
    const flecs::world& world, const BlockQuery& blocks, PhysicsWorld& physics_world, OptionalPalette palette,
    bool changed_only) {
  ReleaseOrphanBodies(world, physics_world);
  blocks.run([&physics_world, palette, changed_only](flecs::iter& it) {
    while (it.next()) {
      if (changed_only && !it.changed()) {
        continue;
      }
      const auto blocks_field = it.field<const z13::station::Block>(0);
      for (const size_t i : it) {
        SyncBlockBody(it.entity(i), blocks_field[i], physics_world, palette);
      }
    }
  });
}

// Syncs only changed block tables; a body count still off afterwards (e.g. a fresh
// PhysicsWorld) forces a full pass.
void SyncBlockBodies(
    const flecs::world& world, PhysicsWorld& physics_world, const BlockQuery& blocks, OptionalPalette palette) {
  // Checked before count(): iterating the query resets its changed state.
  if (blocks.changed()) {
    SyncBlockTables(world, blocks, physics_world, palette, /*changed_only=*/true);
  }
  if (static_cast<size_t>(blocks.count()) != physics_world.BodyCount()) {
    SyncBlockTables(world, blocks, physics_world, palette, /*changed_only=*/false);
  }
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
    const Eigen::Matrix4f& transform, PhysicsWorld& physics_world, const z13::BuildingTuning& tuning) {
  // Station mode destroys through station_module's cell index instead.
  if (player.world().has<z13::station::StationMode>()) {
    return;
  }
  player.remove<z13::building::RequestDestroyBlock>();

  const Eigen::Vector3f origin = z13::math::ExtractTranslation<float>(transform);
  const Eigen::Vector3f forward = transform.block<3, 3>(0, 0).col(0);
  const Eigen::Vector3f target = origin + forward * tuning.destroy_reach_distance;

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

  const BlockQuery blocks =
      world.query_builder<const z13::station::Block>("PhysicsSystem::BlockQuery").detect_changes().build();

  // PostUpdate runs after the building phase, so a block placed this frame gets its
  // body this frame; systems in one phase run in registration order.
  world.system<PhysicsWorld, const BlockPalette*>("PhysicsSystem::SyncBlockBodies")
      .kind<z13::gameplay::PostUpdatePhase>()
      .read<z13::station::Block>()
      .write<RigidBody>()
      .each([blocks](flecs::iter& it, size_t, PhysicsWorld& physics_world, const BlockPalette* palette) {
        SyncBlockBodies(
            it.world(), physics_world, blocks,
            palette != nullptr ? OptionalPalette(palette->palette) : std::nullopt);
      });

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
  world.system<z13::building::RequestDestroyBlock, const z13::gameplay::Player, const Eigen::Matrix4f, PhysicsWorld,
               const z13::BuildingTuning>("PhysicsSystem::ProcessDestroyBlockRequest")
      .kind<z13::gameplay::PostUpdatePhase>()
      .order_by<z13::gameplay::Player>(z13::gameplay::CompareByPlayerId)
      .read<z13::station::StationMode>()
      .write<z13::station::Block>()
      .each(ProcessDestroyBlockRequest);
}

}  // namespace

void PhysicsSystem::Register(flecs::world& world) {
  OnRegisterComponents(world, RegisterComponents);

  OnInitPhases(world, RegisterPipeline);

  OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::bullet_module
