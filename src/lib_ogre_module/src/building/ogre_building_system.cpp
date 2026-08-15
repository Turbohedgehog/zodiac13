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

module;

#include <flecs.h>
#include <Eigen/Dense>
#include <Ogre.h>

#include <lib_core/log.h>

module z13.ogre.building;

import z13.core;
import z13.components;
import z13.ogre.mesh_tools;
import z13.ogre.tools;
import z13.ogre.components;

namespace z13::ogre {

namespace {

struct OgreBuildingBrush {
  int a = 0;
};

void OnBuildingBrushAdded(flecs::entity e, OgreData& ogre_data, const z13::building::Brush& brush, const Eigen::Matrix4f& matrix) {
  e.add<OgreBuildingBrush>();
  CreateCubeMesh(e, *ogre_data.ogre_root, matrix);
}

void OnBuildingBrushRemoved(flecs::entity e, OgreData& ogre_data, const z13::building::Brush& brush) {
  e.remove<OgreBuildingBrush>();
  e.remove<SceneNodeComponent>();
  e.children([](flecs::entity child) {
    if (child.has<SceneNodeComponent>()) {
      child.destruct();
    }
  });
}

void RegisterSystems(flecs::world world) {
  world.observer<OgreData, z13::building::Brush, Eigen::Matrix4f>("OgreBuildingSystem::OnBuildingBrushAdded")
    .event(flecs::OnAdd)
    .without<OgreBuildingBrush>()
    .yield_existing()
    .write<OgreBuildingBrush>()
    .each(OnBuildingBrushAdded);

  world.observer<OgreData, z13::building::Brush>("OgreBuildingSystem::OnBuildingBrushRemoved")
    .event(flecs::OnRemove)
    .with<OgreBuildingBrush>()
    .yield_existing()
    .write<OgreBuildingBrush>()
    .each(OnBuildingBrushRemoved);
}

}  // namespace

void OgreBuildingSystem::Register(flecs::world& world) {
  world.observer<InitSystemsEvent>()
    .event(flecs::OnAdd)
    .yield_existing()
    .each([world = world](const auto&) {
      RegisterSystems(world);
    });
}

}  // namespace z13::ogre
