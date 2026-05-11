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

#include "ogre_system.h"

#include <Eigen/Dense>
#include <Ogre.h>

#include <lib_core/core.h>
#include <lib_core/log.h>
#include <lib_core/math.h>

#include <ogre_module/ogre_components.h>

#include "ogre_tools/ogre_tools.h"
#include <ogre_module/ogre_datatypes.h>
#include <lib_core/components.h>
#include <ogre_module/ogre_components.h>
#include <z13/components/z13.h>
#include <z13/components/gameplay.h>
#include <z13/components/input.h>

#include "private_ogre_components.h"

namespace z13::ogre {

void RegisterPipelines(flecs::world& world) {
  world.component<ReadEvents>().add(flecs::Phase).depends_on(flecs::PreFrame);
  world.get_alive(flecs::PreUpdate).add(flecs::Phase).depends_on<ReadEvents>();

  world.component<PreRender>().add(flecs::Phase).depends_on(flecs::OnStore);
  world.component<Render>().add(flecs::Phase).depends_on<PreRender>();
  world.component<PostRender>().add(flecs::Phase).depends_on<Render>();
  world.component<FinalizeRender>().add(flecs::Phase).depends_on<PostRender>();
  world.get_alive(flecs::PostFrame).add(flecs::Phase).depends_on<FinalizeRender>();
  // world.component<FinalizeRender>().add(flecs::Phase).depends_on<PostRender>();
}

void OnInit(flecs::world world, gameplay::Gameplay) {
  OgreData ogre_data;

  OgreTools::CreateSdlOgreRoot(world, ogre_data);

  auto init_entity = world.entity();
  world.set(ogre_data);
}

void Shutdown(flecs::entity e, OgreWindowClosed, OgreData& ogre_data) {
  OgreTools::DestroySdlOgreWindow(ogre_data);
  e.world().remove<OgreData>();
  e.world().get<CoreComponent>().core->get().Shutdown();
}

void OnAddCamera(flecs::entity e, const gameplay::Camera& camera, OgreData& ogre_data) {
  OgreTools::CreateCamera(e, camera, ogre_data);
}

void OnUpdateOgreSceneNode(SceneNodeComponent& scene_node_component, const Eigen::Matrix4f& transform) {
  OgreTools::UpdateSceneNodeTransform(scene_node_component, transform);
}

void OnRemoveOgreSceneNode(SceneNodeComponent& scene_node_component) {
  auto* scene_manager = scene_node_component.scene_node->getCreator();
  scene_manager->destroySceneNode(scene_node_component.scene_node);
}

void OnRemoveOgreEntity(EntityComponent& entity_component) {
  auto* scene_manager = entity_component.entity->_getManager();
  scene_manager->destroyEntity(entity_component.entity);
}

void OgreSystem::Register(flecs::world& world) {
  RegisterPipelines(world);

  world.component<OgreData>().add(flecs::Singleton);

  world.system<OgreData>("ReadEventsSystem")
    .kind<ReadEvents>()
    .immediate()
    .each([world](auto& ogre_data) { OgreTools::ReadSdlEvents(world, ogre_data); });

  world.system<OgreData>("FinalizeRenderSystem")
    .kind<FinalizeRender>()
    .each([world](auto& ogre_data) { OgreTools::RenderSdlOgreWindow(world, ogre_data); });

  world.observer<OgreWindowClosed, OgreData>("ShutdownObserver")
    .event(flecs::OnAdd)
    .each(Shutdown);

  world.observer<OgreData, gameplay::Pause>("OgreTools::DisableRelativeMouseMode")
    .event(flecs::OnAdd)
    .yield_existing()
    .each(OgreTools::DisableRelativeMouseMode);

  world.observer<gameplay::Pause*>("OgreTools::EnableRelativeMouseMode")
    .event(flecs::OnRemove)
    .yield_existing()
    .each(OgreTools::EnableRelativeMouseMode);

  world.observer<const gameplay::Gameplay>("OgreSystem::OnInit")
    .event(flecs::OnAdd)
    .yield_existing()
    .each([world](const auto& gameplay) { OnInit(world, gameplay); });

  world.observer<const gameplay::Camera, OgreData>("OgreSystem::OnAddCameraObserver")
    .event(flecs::OnAdd)
    .yield_existing()
    .each(OnAddCamera);

  world.observer<SceneNodeComponent, Eigen::Matrix4f>("OgreSystem::OnUpdateOgreSceneNode")
    .event(flecs::OnSet)
    .yield_existing()
    .each(OnUpdateOgreSceneNode);

  world.observer<SceneNodeComponent>("OgreSystem::OnRemoveOgreSceneNode")
    .event(flecs::OnRemove)
    .yield_existing()
    .each(OnRemoveOgreSceneNode);

  world.observer<EntityComponent>("OgreSystem::OnRemoveOgreEntity")
    .event(flecs::OnRemove)
    .yield_existing()
    .each(OnRemoveOgreEntity);
}

}  // namespace z13::ogre
