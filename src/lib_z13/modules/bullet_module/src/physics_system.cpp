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
#include <memory>
#include <optional>
#include <vector>

#include <flecs.h>
#include <Eigen/Dense>

#include <bullet/btBulletDynamicsCommon.h>

#include <bullet_module/bullet_components.h>

#include <lib_core/state/world_serializer.h>
#include <lib_core/state/world_state.h>
#include <lib_core/time/simulation_clock.h>
#include <lib_core/utils/flecs_utils.h>
#include <lib_core/utils/math.h>
#include <lib_core/world/components.h>
#include <lib_core/world/lifecycle.h>
#include <lib_profile/profile.h>

#include <z13/components/block_change_reader.h>
#include <z13/components/block_changes.h>
#include <z13/components/building.h>
#include <z13/components/gameplay.h>
#include <z13/components/gameplay_phases.h>
#include <z13/components/player.h>
#include <z13/components/player_parts.h>
#include <z13/components/gravity.h>
#include <z13/components/input_phases.h>
#include <z13/components/block.h>
#include <z13/components/station_mode.h>
#include <primitives/geometry.h>
#include <primitives/palette.h>
#include <primitives/placement.h>
#include <z13_settings/building_tuning.h>
#include <z13_settings/physics_tuning.h>

namespace z13::bullet_module {

// Where the player stood when the frame began; the collision sweep starts there. Derived
// every frame, so a rollback or teleport between frames is never swept.
struct SweepOrigin {
  Eigen::Vector3f position = Eigen::Vector3f::Zero();
};

namespace {

// Straight down or up, without sliding along what is hit.
constexpr uint32_t kFloorProbeSweeps = 1;
// A probe ending this close to its full length found no floor.
constexpr float kFloorProbeSkin = 1e-4f;
// Meters a rising head is pushed back along the gravity to count as hitting a ceiling.
constexpr float kCeilingPush = 1e-3f;

void RegisterComponents(flecs::world world) {
  z13::flecs_tools::RegisterComponents<PhysicsWorld, RigidBody, SweepOrigin, z13::PhysicsTuning, z13::BuildingTuning>(
      world);
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
  const BlockShapeKey shape = z13::building::primitives::ShapeKeyOf(block.spec);
  physics_world.SyncBody(e.id(), shape, BlockTransform(block), [&shape, palette]() {
    std::vector<z13::building::primitives::ConvexSolid> solids = z13::building::primitives::BlockSolids(palette, shape);
    // Until f/doors opens doors, a closed one lets players through.
    std::erase_if(solids, [](const z13::building::primitives::ConvexSolid& solid) {
      return solid.role == z13::building::primitives::PartRole::kDoorLeaf;
    });
    return z13::building::primitives::Scaled(std::move(solids), z13::station::kCellSize);
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
    const flecs::world& world, const BlockQuery& blocks, PhysicsWorld& physics_world, OptionalPalette palette) {
  Z13_PROFILE_ZONE("SyncBlockTables");
  ReleaseOrphanBodies(world, physics_world);
  blocks.run([&physics_world, palette](flecs::iter& it) {
    while (it.next()) {
      const auto blocks_field = it.field<const z13::station::Block>(0);
      for (const size_t i : it) {
        SyncBlockBody(it.entity(i), blocks_field[i], physics_world, palette);
      }
    }
  });
}

void ApplyBlockChange(const flecs::world& world, const z13::station::BlockChange& change, PhysicsWorld& physics_world,
                      OptionalPalette palette) {
  if (change.kind == z13::station::BlockChangeKind::kRemoved) {
    physics_world.RemoveBody(change.entity);
  } else if (world.is_alive(change.entity)) {
    SyncBlockBody(world.entity(change.entity), change.block, physics_world, palette);
  }
}

// A fresh PhysicsWorld has no bodies, so its count makes the reader resync.
void SyncBlockBodies(const flecs::world& world, PhysicsWorld& physics_world, z13::station::BlockChangeReader& reader,
                     uint64_t restores, uint64_t tick, OptionalPalette palette) {
  const z13::station::BlockChangeBatch batch = reader.Read(restores, tick, physics_world.BodyCount());
  if (batch.resync) {
    SyncBlockTables(world, reader.Blocks(), physics_world, palette);
    return;
  }
  for (const z13::station::BlockChange& change : batch.changes) {
    ApplyBlockChange(world, change, physics_world, palette);
  }
}

btVector3 ToBtVector(const Eigen::Vector3f& vector) {
  return {vector.x(), vector.y(), vector.z()};
}

void RecordSweepOrigin(flecs::entity e, const z13::gameplay::PlayerCollider&, const Eigen::Matrix4f& transform) {
  e.set(SweepOrigin {.position = z13::math::ExtractTranslation<float>(transform)});
}

// Under gravity the collider is the head, kept eye_height above the floor below it.
struct Footing {
  btVector3 down;
  float radius {};
  float legs {};
  float step_height {};
};

struct HeadPath {
  btVector3 origin;
  btVector3 intended;
  btVector3 resolved;
};

// Lands a falling player and steps a walking one up or down within step_height; nullopt
// when a walking player meets a higher floor, an obstacle.
std::optional<btVector3> StandOnFloor(
    const btVector3& head, const Footing& footing, PhysicsWorld& physics_world, z13::gameplay::PlayerMotion& motion) {
  if (motion.fall_speed < 0.f) {
    motion.grounded = false;
    return head;
  }
  const float reach = motion.grounded ? footing.legs + footing.step_height : footing.legs;
  const btVector3 floor_contact =
      physics_world.SweepSphere(head, head + (footing.down * reach), footing.radius, kFloorProbeSweeps);
  const float floor_distance = (floor_contact - head).dot(footing.down);
  if (floor_distance >= reach - kFloorProbeSkin) {
    motion.grounded = false;
    return head;
  }
  if (motion.grounded && footing.legs - floor_distance > footing.step_height) {
    return std::nullopt;
  }
  motion = {.grounded = true};
  const btVector3 standing = floor_contact - (footing.down * footing.legs);
  // Swept, so standing up under a low ceiling stops at it.
  return physics_world.SweepSphere(head, standing, footing.radius, kFloorProbeSweeps);
}

btVector3 ApplyGravity(
    const HeadPath& path, const Footing& footing, PhysicsWorld& physics_world, z13::gameplay::PlayerMotion& motion) {
  // A jump stops at a ceiling.
  if (motion.fall_speed < 0.f && (path.resolved - path.intended).dot(footing.down) > kCeilingPush) {
    motion.fall_speed = 0.f;
  }
  if (const auto stood = StandOnFloor(path.resolved, footing, physics_world, motion)) {
    return *stood;
  }
  // An obstacle too high to step onto stops the walk; the fall along the gravity goes on.
  const btVector3 stopped = path.origin + (footing.down * (path.resolved - path.origin).dot(footing.down));
  if (const auto stood = StandOnFloor(stopped, footing, physics_world, motion)) {
    return *stood;
  }
  motion.grounded = false;
  return stopped;
}

// Mutate-then-e.set() idiom (like BuildingSystem::UpdateBrush), so OnSet
// observers see the correction. Compared exactly: a sweep can stop a step short by
// less than any epsilon. The push-out still runs after the sweep, for blocks placed
// onto the player.
void ResolvePlayerCollision(
    flecs::entity e, const z13::gameplay::PlayerCollider& collider, const SweepOrigin& origin,
    Eigen::Matrix4f& transform, z13::gameplay::PlayerMotion* motion, const z13::gravity::Gravity* gravity,
    PhysicsWorld& physics_world, const z13::PhysicsTuning& tuning) {
  const Eigen::Vector3f position = z13::math::ExtractTranslation<float>(transform);
  const btVector3 swept = physics_world.SweepSphere(
      ToBtVector(origin.position), ToBtVector(position), collider.radius, tuning.max_sweep_iterations);
  btVector3 resolved = physics_world.ResolveSpherePosition(swept, collider.radius);
  if (motion != nullptr && gravity != nullptr && gravity->Pulls()) {
    const Footing footing {
        .down = ToBtVector(gravity->acceleration.normalized()),
        .radius = collider.radius,
        .legs = tuning.eye_height - collider.radius,
        .step_height = tuning.step_height,
    };
    const HeadPath path {.origin = ToBtVector(origin.position), .intended = ToBtVector(position), .resolved = resolved};
    resolved = ApplyGravity(path, footing, physics_world, *motion);
  }
  const Eigen::Vector3f resolved_position(resolved.x(), resolved.y(), resolved.z());
  if (resolved_position == position) {
    return;
  }

  z13::math::SetTranslation(resolved_position, transform);
  e.set(transform);
}

void ProcessDestroyBlockRequest(
    flecs::entity player, z13::building::RequestDestroyBlock, const z13::gameplay::Player& owner,
    const Eigen::Matrix4f& transform, PhysicsWorld& physics_world, const z13::BuildingTuning& tuning,
    z13::gameplay::EditActor& actor) {
  // Station mode destroys through building_module's cell index instead.
  if (player.world().has<z13::station::StationMode>()) {
    return;
  }
  player.remove<z13::building::RequestDestroyBlock>();
  const z13::gameplay::ActorScope scope(actor, owner.id);

  const Eigen::Vector3f origin = z13::math::ExtractTranslation<float>(transform);
  const Eigen::Vector3f forward = transform.block<3, 3>(0, 0).col(0);
  const Eigen::Vector3f target = origin + forward * tuning.destroy_reach_distance;

  const auto hit = physics_world.RaycastEntity(
      btVector3(origin.x(), origin.y(), origin.z()), btVector3(target.x(), target.y(), target.z()));
  if (hit) {
    physics_world.RemoveBody(*hit);
    const flecs::entity block = player.world().entity(*hit);
    z13::station::DestroyBlock(block, block.get<z13::station::Block>());
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
      .run([](flecs::iter& it) { ReconcilePhysicsWorld(it.world()); });

  const auto reader = std::make_shared<z13::station::BlockChangeReader>(world);

  // PostUpdate runs after the building phase, so a block placed this frame gets its
  // body this frame; systems in one phase run in registration order.
  world.system<PhysicsWorld, const z13::flecs_tools::WorldRestores, const z13::flecs_tools::SimulationClock,
               const BlockPalette*>("PhysicsSystem::SyncBlockBodies")
      .kind<z13::gameplay::PostUpdatePhase>()
      .read<z13::station::Block>()
      .read<z13::station::BlockChange>()
      .write<RigidBody>()
      .each([reader](flecs::iter& it, size_t, PhysicsWorld& physics_world,
                     const z13::flecs_tools::WorldRestores& restores, const z13::flecs_tools::SimulationClock& clock,
                     const BlockPalette* palette) {
        SyncBlockBodies(it.world(), physics_world, *reader, restores.count, clock.tick,
                        palette != nullptr ? OptionalPalette(palette->palette) : std::nullopt);
      });

  // Before movement (ApplyActionFramePhase); nothing earlier in the frame moves the player.
  world.system<const z13::gameplay::PlayerCollider, const Eigen::Matrix4f>("PhysicsSystem::RecordSweepOrigin")
      .kind<z13::input::ClearActionFramePhase>()
      .write<SweepOrigin>()
      .each(RecordSweepOrigin);

  world.system<const z13::gameplay::PlayerCollider, const SweepOrigin, Eigen::Matrix4f, z13::gameplay::PlayerMotion*,
               const z13::gravity::Gravity*, PhysicsWorld, const z13::PhysicsTuning>(
           "PhysicsSystem::ResolvePlayerCollision")
      .kind<z13::gameplay::PostUpdatePhase>()
      .each(ResolvePlayerCollision);

  // After ResolvePlayerCollision in the same phase, so the raycast sees the final player
  // transform; a later phase would run after rendering. write<> merges the destroy early.
  world.system<z13::building::RequestDestroyBlock, const z13::gameplay::Player, const Eigen::Matrix4f, PhysicsWorld,
               const z13::BuildingTuning, z13::gameplay::EditActor>("PhysicsSystem::ProcessDestroyBlockRequest")
      .kind<z13::gameplay::PostUpdatePhase>()
      .order_by<z13::gameplay::Player>(z13::gameplay::CompareByPlayerId)
      .read<z13::station::StationMode>()
      .write<z13::station::Block>()
      .write<z13::station::BlockChange>()
      .each(ProcessDestroyBlockRequest);
}

}  // namespace

void PhysicsSystem::Register(flecs::world& world) {
  OnRegisterComponents(world, RegisterComponents);

  OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::bullet_module
