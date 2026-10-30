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

#include "conduit_mesh_system.h"

#include <utility>

#include <flecs.h>

#include <lib_core/state/world_state.h>
#include <lib_core/time/simulation_clock.h>
#include <lib_core/world/lifecycle.h>

#include <z13/components/conduit.h>
#include <z13/components/scene_mesh.h>

#include "conduit_look.h"
#include "conduit_mesh.h"

namespace z13::conduit_render {

namespace {

using z13::conduit::Conduit;
using z13::conduit::ConduitKinds;
using z13::render::SceneMesh;
using z13::render::SceneMeshPhase;

void RegisterComponents(flecs::world world) {
  z13::flecs_tools::RegisterComponents<ConduitLook, SceneMesh>(world);
}

void RegisterPhases(flecs::world world) {
  // As raylib_module declares it, which also puts it before its sync.
  world.component<SceneMeshPhase>().add(flecs::Phase).add<z13::flecs_tools::PresentationPhase>();
}

// A conduit whose kind the file lacks has no mesh.
void DeriveMesh(flecs::entity entity, const Conduit& conduit, const ConduitKinds& kinds) {
  const auto kind = kinds.Find(conduit.kind);
  if (!kind) {
    entity.remove<SceneMesh>();
    entity.remove<ConduitLook>();
    return;
  }
  const ConduitLook look {.conduit = conduit, .color = kind->color, .thickness_cells = kind->thickness_cells};
  const auto* built = entity.try_get<ConduitLook>();
  if (built != nullptr && *built == look) {
    return;
  }
  SceneMesh mesh = BuildConduitMesh(conduit, *kind);
  const auto* old = entity.try_get<SceneMesh>();
  mesh.revision = old != nullptr ? old->revision + 1 : 1;
  entity.set(look);
  entity.set(std::move(mesh));
}

void RegisterSystems(flecs::world world) {
  world.system<const Conduit, const ConduitKinds>("ConduitMeshSystem::DeriveMesh")
      .kind<SceneMeshPhase>()
      .write<SceneMesh>()
      .write<ConduitLook>()
      .each(DeriveMesh);
}

}  // namespace

void ConduitMeshSystem::Register(flecs::world& world) {
  z13::OnRegisterComponents(world, RegisterComponents);
  z13::OnInitPhases(world, RegisterPhases);
  z13::OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::conduit_render
