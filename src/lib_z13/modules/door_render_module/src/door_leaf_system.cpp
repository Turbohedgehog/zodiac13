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

#include <functional>
#include <utility>

#include <flecs.h>

#include <lib_core/state/world_state.h>
#include <lib_core/time/simulation_clock.h>
#include <lib_core/world/lifecycle.h>

#include <primitives/palette.h>
#include <z13/components/block.h>
#include <z13/components/door.h>
#include <z13/components/scene_mesh.h>

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
  z13::flecs_tools::RegisterComponents<DoorLeafLook, SceneMesh>(world);
}

void RegisterPhases(flecs::world world) {
  // As raylib_module declares it, which also puts it before its sync.
  world.component<SceneMeshPhase>().add(flecs::Phase).add<z13::flecs_tools::PresentationPhase>();
}

void DeriveLeaf(flecs::entity door, const DoorState& state, const Block& block, const BlockPalette& palette) {
  const DoorLeafLook look {.block = block, .position = state.position};
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
  const auto* old = door.try_get<SceneMesh>();
  mesh->revision = old != nullptr ? old->revision + 1 : 1;
  door.set(std::move(*mesh));
}

void RegisterSystems(flecs::world world) {
  world.system<const DoorState, const Block, const BlockPalette>("DoorLeafSystem::DeriveLeaf")
      .kind<SceneMeshPhase>()
      .write<SceneMesh>()
      .write<DoorLeafLook>()
      .each(DeriveLeaf);
}

}  // namespace

void DoorLeafSystem::Register(flecs::world& world) {
  z13::OnRegisterComponents(world, RegisterComponents);
  z13::OnInitPhases(world, RegisterPhases);
  z13::OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::door_render
