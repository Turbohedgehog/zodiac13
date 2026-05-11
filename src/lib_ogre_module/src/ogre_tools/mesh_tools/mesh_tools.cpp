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

#include "mesh_tools.h"

#include <flecs.h>
#include <Ogre.h>
#include <fmt/format.h>

#include <lib_core/log.h>

#include "../ogre_tools.h"
#include "../../private_ogre_components.h"

namespace z13::ogre {

void CreateCubeMesh(flecs::entity& parent_entity, Ogre::Root& ogre_root, const Eigen::Matrix4f&) {
  auto* scene_manager = OgreTools::GetSceneManager(ogre_root);
  auto* manual_object = scene_manager->createManualObject();

  manual_object->begin("BaseWhiteNoLighting", Ogre::RenderOperation::OT_TRIANGLE_LIST);
  static constexpr float kHalfSize = 0.3f;
  // Вершины одной грани (для примера — передняя)
  manual_object->position(-kHalfSize,  kHalfSize,  kHalfSize); // 0
  manual_object->position( kHalfSize,  kHalfSize,  kHalfSize); // 1
  manual_object->position( kHalfSize, -kHalfSize,  kHalfSize); // 2
  manual_object->position(-kHalfSize, -kHalfSize,  kHalfSize); // 3

  // Задняя грань
  manual_object->position(-kHalfSize,  kHalfSize, -kHalfSize); // 4
  manual_object->position( kHalfSize,  kHalfSize, -kHalfSize); // 5
  manual_object->position( kHalfSize, -kHalfSize, -kHalfSize); // 6
  manual_object->position(-kHalfSize, -kHalfSize, -kHalfSize); // 7

  // Перед (+Z)
  manual_object->triangle(0, 2, 1); manual_object->triangle(0, 3, 2);
  // Зад (-Z)
  manual_object->triangle(5, 7, 4); manual_object->triangle(5, 6, 7);
  // Право (+X)
  manual_object->triangle(1, 6, 5); manual_object->triangle(1, 2, 6);
  // Лево (-X)
  manual_object->triangle(4, 3, 0); manual_object->triangle(4, 7, 3);
  // Верх (+Y)
  manual_object->triangle(4, 1, 5); manual_object->triangle(4, 0, 1);
  // Низ (-Y)
  manual_object->triangle(3, 6, 2); manual_object->triangle(3, 7, 6);
  manual_object->end();

  auto* parent_scene_node = scene_manager->getRootSceneNode()->createChildSceneNode();
  auto* mid_scene_node = parent_scene_node->createChildSceneNode();

  LOG_INFO("~~~~ 1 CreateCubeMesh = {}", reinterpret_cast<uint64_t>(parent_scene_node));

  parent_entity
    // .set<Eigen::Matrix4f>(Eigen::Matrix4f::Identity())
    .set<SceneNodeComponent>({ .scene_node = parent_scene_node });

  std::string name = fmt::format("Entity_{}", parent_entity.id());
  // std::string name = parent_entity.name().c_str();

  Ogre::MeshPtr mesh = manual_object->convertToMesh(name);
  Ogre::Entity* entity = scene_manager->createEntity(name, name);
  scene_manager->destroyManualObject(manual_object);
  mid_scene_node->attachObject(entity);

  LOG_INFO("~~~~ 2 CreateCubeMesh = {}", reinterpret_cast<uint64_t>(mid_scene_node));

  parent_entity.world().entity()
    .child_of(parent_entity)
    .set<Eigen::Matrix4f>(Eigen::Matrix4f::Identity())
    .set<SceneNodeComponent>({ .scene_node = mid_scene_node })
    .set<EntityComponent>({ .entity = entity, });
}

}  // namespace z13::ogre
