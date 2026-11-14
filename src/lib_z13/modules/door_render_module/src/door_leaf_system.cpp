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


#include "door_leaf_system.h"

#include <algorithm>
#include <format>
#include <functional>
#include <iterator>
#include <string>
#include <vector>
#include <memory>
#include <utility>

#include <flecs.h>

#include <lib_core/state/world_state.h>
#include <lib_core/time/simulation_clock.h>
#include <lib_core/world/lifecycle.h>

#include <primitives/palette.h>
#include <z13/components/gameplay/access.h>
#include <z13/components/gameplay/player.h>
#include <z13/components/station/block.h>
#include <z13/components/station/door.h>
#include <z13/components/station/grid.h>
#include <z13/components/station/grid_pose.h>
#include <z13/components/gameplay/scene_mesh.h>

#include "door_lamp.h"
#include "door_leaf_look.h"
#include "door_leaf_mesh.h"

namespace z13::door_render {

namespace {

using z13::building::primitives::BlockPalette;
using z13::render::SceneMesh;
using z13::render::SceneMeshPhase;
using z13::station::Block;
using z13::station::DoorState;

void RegisterComponents(flecs::world world) {
  z13::flecs_tools::RegisterComponents<DoorLeafLook, DoorLampLook, DoorLampOf, SceneMesh>(world);
}

void RegisterPhases(flecs::world world) {
  // As raylib_module declares it, which also puts it before its sync.
  world.component<SceneMeshPhase>().add(flecs::Phase).add<z13::flecs_tools::PresentationPhase>();
}

void SetInWorld(SceneMesh& mesh, const Eigen::Matrix4f& pose) {
  if (pose.isIdentity()) {
    return;
  }
  const Eigen::Affine3f transform(pose);
  for (Eigen::Vector3f& position : mesh.positions) {
    position = transform * position;
  }
  for (Eigen::Vector3f& normal : mesh.normals) {
    normal = transform.linear() * normal;
  }
}

void DeriveLeaf(flecs::entity door, const DoorState& state, const Block& block, const BlockPalette& palette,
                const Eigen::Matrix4f& pose) {
  const DoorLeafLook look {.block = block, .position = state.position, .pose = pose};
  const auto* built = door.try_get<DoorLeafLook>();
  if (built != nullptr && *built == look) {
    return;
  }
  door.set(look);
  auto mesh = BuildLeafMesh(block, state.position, std::cref(palette.palette));
  if (!mesh) {
    door.remove<SceneMesh>();
    return;
  }
  SetInWorld(*mesh, pose);
  const auto* old = door.try_get<SceneMesh>();
  mesh->revision = old != nullptr ? old->revision + 1 : 1;
  door.set(std::move(*mesh));
}

// What a door's lamp says to the local player, worked out once a tick for all doors.
struct LampContext {
  uint64_t tick {};
  bool fresh {};
  flecs::entity player;
  std::vector<z13::inventory::AccessLock> locks;
};

void RefreshContext(const flecs::world& world, uint64_t tick, LampContext& context) {
  if (context.fresh && context.tick == tick) {
    return;
  }
  context = {.tick = tick, .fresh = true};
  const auto* local = world.try_get<z13::gameplay::LocalPlayer>();
  if (local != nullptr && local->id) {
    world.each([&](flecs::entity entity, const z13::gameplay::Player& player) {
      if (player.id == *local->id) {
        context.player = entity;
      }
    });
  }
  world.each([&](const z13::inventory::AccessLock& lock) { context.locks.push_back(lock); });
}

DoorSignal SignalFor(const flecs::world& world, flecs::entity door, const Block& block, const LampContext& context) {
  std::vector<z13::inventory::AccessLock> locks;
  std::ranges::copy_if(context.locks, std::back_inserter(locks), [&block](const z13::inventory::AccessLock& lock) {
    return lock.grid == block.grid && lock.cell == block.cell;
  });
  const bool may_use = !context.player.is_valid() || z13::inventory::MayUse(world, context.player, door, locks);
  return SignalOf(door.has<z13::station::DoorInterlock>(), may_use);
}

void DeriveLamp(flecs::entity door, const Block& block, const BlockPalette& palette, const Eigen::Matrix4f& pose,
                DoorSignal signal) {
  const flecs::world world = door.world();
  const std::string name = std::format("DoorLamp_{}", door.id());
  const flecs::entity lamp = world.entity(name.c_str());
  const DoorLampLook look {.block = block, .signal = signal, .pose = pose};
  const auto* built = lamp.try_get<DoorLampLook>();
  if (built != nullptr && *built == look) {
    return;
  }
  lamp.set(DoorLampOf {.door = door.id()});
  lamp.set(look);
  auto mesh = BuildLampMesh(block, signal, std::cref(palette.palette));
  if (!mesh) {
    lamp.remove<SceneMesh>();
    return;
  }
  SetInWorld(*mesh, pose);
  mesh->grid = block.grid;
  const auto* old = lamp.try_get<SceneMesh>();
  mesh->revision = old != nullptr ? old->revision + 1 : 1;
  lamp.set(std::move(*mesh));
}

void RegisterSystems(flecs::world world) {
  const auto grids = world.query_builder<const z13::station::Grid>().detect_changes().build();
  world.system<const DoorState, const Block, const BlockPalette>("DoorLeafSystem::DeriveLeaf")
      .kind<SceneMeshPhase>()
      .read<z13::station::Grid>()
      .read<z13::station::DoorInterlock>()
      .read<z13::inventory::AccessLock>()
      .write<SceneMesh>()
      .write<DoorLeafLook>()
      .each([grids, poses = std::make_shared<z13::station::GridPoses>(), context = std::make_shared<LampContext>()](
                flecs::entity door, const DoorState& state, const Block& block, const BlockPalette& palette) {
        if (grids.changed()) {
          *poses = z13::station::PosesOf(grids);
        }
        const Eigen::Matrix4f pose = z13::station::PoseOfGrid(*poses, block.grid).matrix();
        DeriveLeaf(door, state, block, palette, pose);
        const flecs::world world = door.world();
        RefreshContext(world, world.get<z13::flecs_tools::SimulationClock>().tick, *context);
        DeriveLamp(door, block, palette, pose, SignalFor(world, door, block, *context));
      });
  world.system<const DoorLampOf>("DoorLeafSystem::DropLamps")
      .kind<SceneMeshPhase>()
      .each([](flecs::entity lamp, const DoorLampOf& of) {
        if (!lamp.world().is_alive(of.door)) {
          lamp.destruct();
        }
      });
}

}  // namespace

void DoorLeafSystem::Register(flecs::world& world) {
  z13::OnRegisterComponents(world, RegisterComponents);
  z13::OnInitPhases(world, RegisterPhases);
  z13::OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::door_render
