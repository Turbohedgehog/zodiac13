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
#include <optional>

#include <flecs.h>
#include <Eigen/Dense>


#include <z13/components/building.h>
#include <z13/components/gameplay.h>

#include <lib_core/components.h>
#include <lib_core/math.h>
#include <lib_core/log.h>



namespace z13::building {

namespace {

struct UpdateBuildingToolPhase {};

void RegisterPipeline(flecs::world world) {
  world.component<UpdateBuildingToolPhase>().add(flecs::Phase).depends_on<z13::gameplay::UpdatePhase>();
  world.component<z13::gameplay::PostUpdatePhase>().add(flecs::Phase).depends_on<UpdateBuildingToolPhase>();
  // world.component<z13::gameplay::UpdatePhase>().add(flecs::Phase).depends_on<UpdateBuildingToolPhase>();
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

void OnEnableBuildingTool(flecs::entity e, const BuildingTool& building_tool, const Eigen::Matrix4f& parent_transform) {
  auto brush = Brush {
    .distance = 5.f,
  };
  
  auto brush_entity = e.world().entity().child_of(e).set(brush);
  // log_info("~~~ OnEnableBuildingTool = {} -> {}", e.id(), brush_entity.id());
  Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
  UpdateBrush(brush_entity, parent_transform, brush, transform);
}

void OnDisableBuildingTool(flecs::entity e, const BuildingTool& building_tool) {
  // log_info("~~~ OnDisableBuildingTool 1 = {}", e.id());
  e.children([](flecs::entity child) {
    // if (child.has<Brush>(flecs::System)) {
    if (child.has<Brush>()) {
      // log_info("~~~ OnDisableBuildingTool 2 = {}", child.id());
      child.destruct();
    }
  });
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

void ProcessBuildBlockRequest(flecs::entity player, RequestBuildBlock) {
  player.remove<RequestBuildBlock>();

  const auto brush_transform = FindBrushTransform(player);
  if (!brush_transform) {
    return;
  }

  // Named so world save/load (CaptureWorld, world_serializer.cpp) picks it
  // up -- only named entities are captured.
  flecs::entity block = player.world().entity();
  block.set_name(std::format("BasicBlock_{}", block.id()).c_str());
  block.set(*brush_transform).add<BasicBlock>();
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
  world.observer<BuildingTool, Eigen::Matrix4f>("BuildingSystem::OnDisableBuildingTool")
    .event(flecs::OnAdd)
    .yield_existing()
    .each(OnEnableBuildingTool);
    
  world.observer<BuildingTool>("BuildingSystem::OnEnableBuildingTool")
    .event(flecs::OnRemove)
    .yield_existing()
    .each(OnDisableBuildingTool);

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
  world.system<RequestBuildBlock>("BuildingSystem::ProcessBuildBlockRequest")
    .kind<UpdateBuildingToolPhase>()
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
