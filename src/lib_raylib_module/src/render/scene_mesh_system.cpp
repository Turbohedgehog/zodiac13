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

#include "scene_mesh_system.h"

#include <vector>

#include <flecs.h>

#include <lib_core/state/world_state.h>
#include <lib_core/time/simulation_clock.h>
#include <lib_core/world/lifecycle.h>

#include <raylib_module/raylib_components.h>
#include <z13/components/scene_mesh.h>

#include "render_components.h"
#include "scene_meshes.h"

namespace z13::raylib {

namespace {

using z13::render::SceneMesh;
using z13::render::SceneMeshPhase;

void RegisterComponents(flecs::world world) {
  z13::flecs_tools::RegisterComponents<SceneMesh, SceneMeshes>(world);
}

void RegisterPhases(flecs::world world) {
  using z13::flecs_tools::PresentationPhase;
  world.component<SceneMeshPhase>().add(flecs::Phase).add<PresentationPhase>().depends_on<RenderGatePhase>();
  world.component<PreRender>().depends_on<SceneMeshPhase>();
}

void RegisterSystems(flecs::world world) {
  const auto meshes = world.query_builder<const SceneMesh>("SceneMeshSystem::Meshes").build();
  world.system<SceneMeshes, const Lighting>("SceneMeshSystem::Sync")
      .kind<Render>()
      .tick_source<RenderGate>()
      .read<SceneMesh>()
      .each([meshes](SceneMeshes& scene_meshes, const Lighting& lighting) {
        std::vector<SceneMeshEntry> entries;
        meshes.each([&entries](flecs::entity entity, const SceneMesh& mesh) {
          entries.push_back({.entity = entity.id(), .mesh = mesh});
        });
        scene_meshes.Sync(entries, lighting.res ? *lighting.res->shader : ::Shader {});
      });
}

}  // namespace

void SceneMeshSystem::Register(flecs::world& world) {
  z13::OnRegisterComponents(world, RegisterComponents);
  z13::OnInitPhases(world, RegisterPhases);
  z13::OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::raylib
