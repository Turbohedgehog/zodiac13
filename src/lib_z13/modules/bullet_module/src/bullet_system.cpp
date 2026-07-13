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

#include "bullet_system.h"

#include <btBulletDynamicsCommon.h>
#include <flecs.h>

#include <lib_core/log.h>

#include <z13/components/gameplay.h>

#include "bullet_components.h"

namespace z13::bullet {

void OnInit(flecs::world world, gameplay::Gameplay) {
  LOG_INFO("BulletSystem::OnInit");

  auto& bullet_data = world.ensure<BulletData>();
  bullet_data.collision_configuration = std::make_shared<btDefaultCollisionConfiguration>();
  bullet_data.collision_dispatcher = std::make_shared<btCollisionDispatcher>(bullet_data.collision_configuration.get());
  bullet_data.broadphase_interface = std::make_shared<btDbvtBroadphase>();
  bullet_data.sequential_impulse_constraint_solver = std::make_shared<btSequentialImpulseConstraintSolver>();
  bullet_data.dynamics_world = std::make_shared<btDiscreteDynamicsWorld>(
    bullet_data.collision_dispatcher.get(),
    bullet_data.broadphase_interface.get(),
    bullet_data.sequential_impulse_constraint_solver.get(),
    bullet_data.collision_configuration.get());
}

void BulletSystem::Register(flecs::world& world) {
  world.observer<const gameplay::Gameplay>("BulletSystem::OnInit")
    .event(flecs::OnAdd)
    .yield_existing()
    .each([world](const auto& gameplay) { OnInit(world, gameplay); });
}

}  // namespace z13::bullet
