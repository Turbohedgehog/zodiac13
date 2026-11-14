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

#include <algorithm>
#include <utility>
#include <vector>

#include <flecs.h>

#include <lib_core/state/world_state.h>
#include <lib_core/time/simulation_clock.h>
#include <lib_core/world/lifecycle.h>

#include <z13/components/station/conduit.h>
#include <z13/components/station/grid_pose.h>
#include <z13/components/gameplay/scene_mesh.h>

#include "conduit_cells.h"
#include "conduit_look.h"
#include "conduit_mesh.h"

namespace z13::conduit_render {

namespace {

using z13::conduit::Conduit;
using z13::conduit::ConduitKind;
using z13::conduit::ConduitKinds;
using z13::render::SceneMesh;
using z13::render::SceneMeshPhase;

void RegisterComponents(flecs::world world) {
  z13::flecs_tools::RegisterComponents<ConduitLook, SceneMesh, ConduitCells>(world);
}

void RegisterPhases(flecs::world world) {
  // As raylib_module declares it, which also puts it before its sync.
  world.component<SceneMeshPhase>().add(flecs::Phase).add<z13::flecs_tools::PresentationPhase>();
}

// Whether the cell past an end of the run holds a run turning another way.
RunCorners CornersOf(const Conduit& conduit, const ConduitKind& kind, const ConduitCells& cells) {
  const auto axis = static_cast<int>(conduit.axis);
  Eigen::Vector3i before = conduit.cell;
  before[axis] -= 1;
  Eigen::Vector3i after = conduit.cell;
  after[axis] += conduit.length;
  return {.low = cells.Turns(kind.network, conduit.grid, before, conduit.axis),
          .high = cells.Turns(kind.network, conduit.grid, after, conduit.axis)};
}

// The mesh is in the run's grid meters; the grid's pose moves it into the world.
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

// A conduit whose kind the file lacks has no mesh.
void DeriveMesh(flecs::entity entity, const Conduit& conduit, const ConduitKinds& kinds, const ConduitCells& cells) {
  const auto* built = entity.try_get<ConduitLook>();
  if (built != nullptr && built->generation == cells.Generation()) {
    return;
  }
  const auto kind = kinds.Find(conduit.kind);
  if (!kind) {
    entity.remove<SceneMesh>();
    entity.remove<ConduitLook>();
    return;
  }
  const ConduitLook look {.conduit = conduit,
                          .corners = CornersOf(conduit, *kind, cells),
                          .color = kind->color,
                          .thickness_cells = kind->thickness_cells,
                          .pose = cells.PoseOf(conduit.grid),
                          .generation = cells.Generation()};
  if (built != nullptr && built->SameLook(look)) {
    entity.set(look);
    return;
  }
  SceneMesh mesh = BuildConduitMesh(conduit, *kind, look.corners);
  SetInWorld(mesh, look.pose);
  const auto* old = entity.try_get<SceneMesh>();
  mesh.revision = old != nullptr ? old->revision + 1 : 1;
  entity.set(look);
  entity.set(std::move(mesh));
}

void RegisterSystems(flecs::world world) {
  // Set here, not next to `.add(flecs::Singleton)` (see PhysicsSystem::RegisterSystems).
  world.set(ConduitCells {});
  const auto conduits = world.query_builder<const Conduit>().detect_changes().build();
  const auto grids = world.query<const z13::station::Grid>();
  world.system<ConduitCells, const ConduitKinds>("ConduitMeshSystem::UpdateCells")
      .kind<SceneMeshPhase>()
      .read<Conduit>()
      .read<z13::station::Grid>()
      .each([conduits, grids](ConduitCells& cells, const ConduitKinds& kinds) {
        cells.SetPoses(z13::station::PosesOf(grids));
        if (conduits.changed()) {
          cells.Rebuild(SortedConduits(conduits), kinds);
        }
      });

  world.system<const Conduit, const ConduitKinds, const ConduitCells>("ConduitMeshSystem::DeriveMesh")
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
