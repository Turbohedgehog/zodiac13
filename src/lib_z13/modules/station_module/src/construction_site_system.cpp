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

#include "construction_site_system.h"

#include <format>
#include <string_view>

#include <flecs.h>
#include <Eigen/Dense>

#include <lib_core/state/world_state.h>
#include <lib_core/utils/math.h>
#include <lib_core/world/lifecycle.h>

#include <z13/components/building.h>
#include <z13/components/gameplay.h>
#include <z13/components/station.h>

namespace z13::station {

namespace {

// The slab is BasicBlocks until station primitives replace them (f/block-grid).
constexpr int kSlabHalfWidthInBlocks = 3;
// Gap between the spawn point, at the origin, and the slab's top face.
constexpr float kSpawnHeightAboveSlab = 1.f;
constexpr std::string_view kSpawnPointName = "StationSpawnPoint";

Eigen::Matrix4f At(const Eigen::Vector3f& position) {
  Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
  z13::math::SetTranslation(position, transform);
  return transform;
}

void PopulateConstructionSite(flecs::entity e, z13::gameplay::PopulateSceneEvent) {
  flecs::world world = e.world();
  if (!world.has<StationMode>()) {
    return;
  }

  constexpr float kBlockSize = z13::building::kBlockSize;
  const float slab_z = -kSpawnHeightAboveSlab - kBlockSize / 2.f;
  for (int x = -kSlabHalfWidthInBlocks; x <= kSlabHalfWidthInBlocks; ++x) {
    for (int y = -kSlabHalfWidthInBlocks; y <= kSlabHalfWidthInBlocks; ++y) {
      world.entity(std::format("StationSlab_{}_{}", x, y).c_str())
          .add<z13::flecs_tools::StateEntity>()
          .set(At({static_cast<float>(x) * kBlockSize, static_cast<float>(y) * kBlockSize, slab_z}))
          .add<z13::building::BasicBlock>();
    }
  }
  world.entity(kSpawnPointName.data())
      .add<z13::flecs_tools::StateEntity>()
      .set(At(Eigen::Vector3f::Zero()))
      .add<SpawnPoint>();
}

void RegisterSystems(flecs::world world) {
  world.observer<z13::gameplay::PopulateSceneEvent>("ConstructionSiteSystem::PopulateConstructionSite")
      .event<z13::gameplay::PopulateSceneEvent>()
      .each(PopulateConstructionSite);
}

}  // namespace

void ConstructionSiteSystem::Register(flecs::world& world) {
  OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::station
