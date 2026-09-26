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

#include "building_system.h"

#include <format>
#include <string>
#include <optional>

#include <flecs.h>
#include <Eigen/Dense>


#include <z13/components/building.h>
#include <z13/components/gameplay.h>
#include <z13/components/input.h>

#include <lib_core/components.h>
#include <lib_core/math.h>
#include <lib_core/world_state.h>
#include <lib_core/log.h>



namespace z13::building {

namespace {

struct UpdateBuildingToolPhase {};

void RegisterPipeline(flecs::world world) {
  // BuildingTool is toggled in ApplyActionFramePhase; SyncBrush below must see that
  // same frame, so this replaces (not adds to) UpdateBuildingToolPhase's dependency.
  world.component<UpdateBuildingToolPhase>().add(flecs::Phase).depends_on<z13::input::ApplyActionFramePhase>();
  world.component<z13::gameplay::PostUpdatePhase>().add(flecs::Phase).depends_on<UpdateBuildingToolPhase>();
  // A "late system" (flecs::OnStore, e.g. render) must see this frame's brush/block
  // removals too -- explicit, since OnStore's own default position isn't guaranteed to
  // trail a custom phase this far downstream of ApplyActionFramePhase.
  world.get_alive(flecs::OnStore).add(flecs::Phase).depends_on<z13::gameplay::PostUpdatePhase>();
}

void UpdateBrush(
    flecs::entity e,
    const Eigen::Matrix4f& parent_transform,
    const Brush& brush,
    Eigen::Matrix4f& brush_transform) {
  Eigen::Vector3f dir { brush.distance, 0.f, 0.f };
  // Eigen::Vector3f pos = z13::math::ExtractTranslation(parent_transform) + dir;
  auto pos = Eigen::Vector3f((parent_transform * dir.homogeneous()).head<3>());
  auto prev_pos = z13::math::ExtractTranslation(brush_transform);
  if (!z13::math::IsNear(pos, prev_pos)) {
    // log_info("UpdateBrush = {}, {}, {}", pos.x(), pos.y(), pos.z());
    z13::math::SetTranslation(pos, brush_transform);
    // log_info("~~~ UpdateBrush");
    // notify all
    e.set(brush_transform);
  }
}

void CreateBrush(flecs::entity player, const Eigen::Matrix4f& parent_transform) {
  auto brush = Brush {
    .distance = 5.f,
  };

  // Always give the brush a transform: UpdateBrush only sets one when the brush moved,
  // and a brush created exactly at the world origin would otherwise have none.
  Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
  auto brush_entity = player.world().entity().child_of(player).set(brush).set(transform);
  UpdateBrush(brush_entity, parent_transform, brush, transform);
}

// The brush is derived from the BuildingTool tag each frame (exactly one while the
// tool is on), so it stays consistent however the tag got there.
void SyncBrush(flecs::entity player, const BuildingTool&, const Eigen::Matrix4f& parent_transform) {
  bool has_brush = false;
  player.children([&has_brush](flecs::entity child) {
    if (!child.has<Brush>()) {
      return;
    }
    if (has_brush) {
      child.destruct();
    }
    has_brush = true;
  });

  if (!has_brush) {
    CreateBrush(player, parent_transform);
  }
}

void ReleaseOrphanBrush(flecs::entity brush, const Brush&) {
  const flecs::entity owner = brush.parent();
  if (!owner || !owner.has<BuildingTool>()) {
    brush.destruct();
  }
}

std::optional<Eigen::Matrix4f> FindBrushTransform(flecs::entity player) {
  std::optional<Eigen::Matrix4f> transform;
  player.children([&transform](flecs::entity child) {
    if (!transform && child.has<Brush>()) {
      transform = child.get<Eigen::Matrix4f>();
    }
  });

  return transform;
}

// Named after a state counter so names stay unique across save/load; a counter that
// lags behind existing names (hand-edited or older save) skips the taken ones.
//
// Temporary: probing world.lookup() in a loop is a brute-force way to find a free
// name and doesn't scale. Revisit once name collisions actually show up in practice.
flecs::entity SpawnBlock(
    flecs::world world, z13::gameplay::IdCounters& counters, const Eigen::Matrix4f& transform) {
  std::string name;
  do {
    name = std::format("Block_{}", ++counters.last_block_id);
  } while (world.lookup(name.c_str()));

  flecs::entity block = world.entity(name.c_str());
  return block.add<z13::flecs_tools::StateEntity>().set(transform).add<BasicBlock>();
}

void ProcessBuildBlockRequest(flecs::entity player, RequestBuildBlock, z13::gameplay::IdCounters& counters) {
  player.remove<RequestBuildBlock>();

  const auto brush_transform = FindBrushTransform(player);
  if (!brush_transform) {
    return;
  }

  SpawnBlock(player.world(), counters, *brush_transform);
}

void UpdateBuildingTool(
    flecs::entity e,
    const z13::gameplay::Player&,
    const BuildingTool& building_tool,
    const Eigen::Matrix4f&) {
}

void AppendBuildingTool(flecs::entity e, const z13::gameplay::Player&) {
  e.add<BuildingTool>();
}

void RegisterSystems(flecs::world world) {
  // Registered before UpdateBrush (same phase, so runs before it); the write<>
  // terms make flecs merge a new brush before UpdateBrush and the block request.
  world.system<const Brush>("BuildingSystem::ReleaseOrphanBrush")
    .kind<UpdateBuildingToolPhase>()
    .read<BuildingTool>()
    .write<Brush>()
    .each(ReleaseOrphanBrush);

  world.system<const BuildingTool, const Eigen::Matrix4f>("BuildingSystem::SyncBrush")
    .kind<UpdateBuildingToolPhase>()
    .write<Brush>()
    .write<Eigen::Matrix4f>()
    .each(SyncBrush);

  world.system<Eigen::Matrix4f, Brush, Eigen::Matrix4f>("BuildingSystem::UpdateBrush")
    .kind<UpdateBuildingToolPhase>()
    .without<z13::gameplay::Pause>()
    .term_at(0).parent()
    .each(UpdateBrush);

  world.system<z13::gameplay::Player, BuildingTool, Eigen::Matrix4f>("BuildingSystem::UpdateBuildingTool")
    .kind<UpdateBuildingToolPhase>()
    .without<z13::gameplay::Pause>()
    .each(UpdateBuildingTool);

  // Registered after UpdateBrush above (same phase, so runs after it): reads
  // the brush position UpdateBrush just refreshed this frame.
  // The write<> terms make flecs merge the spawned block before systems that read
  // it later in the frame (e.g. bullet's body sync).
  world.system<RequestBuildBlock, z13::gameplay::IdCounters>("BuildingSystem::ProcessBuildBlockRequest")
    .kind<UpdateBuildingToolPhase>()
    .write<BasicBlock>()
    .write<Eigen::Matrix4f>()
    .each(ProcessBuildBlockRequest);

  // RequestDestroyBlock itself is handled in bullet_module (raycast against
  // PhysicsWorld's block bodies), which owns the only class that can do it.
}

}  // namespace

void BuildingSystem::Register(flecs::world& world) {
  world.observer<InitPhasesEvent>("BuildingSystem::RegisterPipeline")
    .event(flecs::OnSet)
    .yield_existing()
    .each([world = world](const auto&) { RegisterPipeline(world); });

  world.observer<InitSystemsEvent>("BuildingSystem::RegisterSystems")
    .event(flecs::OnSet)
    .yield_existing()
    .each([world = world](const auto&) { RegisterSystems(world); });  
}

}  // namespace z13::building
