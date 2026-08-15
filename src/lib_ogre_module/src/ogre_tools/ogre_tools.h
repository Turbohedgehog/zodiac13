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

#pragma once

#include <flecs.h>
#include <Eigen/Dense>

#include <ogre_module/ogre_datatypes.h>

namespace z13::gameplay {

struct Gameplay;
struct Pause;
struct Camera;

}  // namespace z13::gameplay

namespace z13::geometry {

struct Transform;

}  // namespace z13::geometry

namespace z13::input {

struct SystemInputEventType;

}  // namespace z13::input

namespace z13::ogre {

struct OgreData;

class OgreTools {
 public:
  static void CreateSdlOgreRoot(flecs::world world, OgreData& ogre_data);
  static void ReadSdlEvents(flecs::world world, OgreData& ogre_data);
  static void RenderSdlOgreWindow(flecs::world world, OgreData& ogre_data);
  static void DestroySdlOgreWindow(OgreData& ogre_data);
  static void EnableRelativeMouseMode(const gameplay::Pause*);
  static void DisableRelativeMouseMode(const OgreData& ogre_data, const gameplay::Pause&);
  static void CreateCamera(flecs::entity e, const gameplay::Camera& camera, const OgreData& ogre_data);
  static void UpdateSceneNodeTransform(struct SceneNodeComponent& scene_node_component, const Eigen::Matrix4f& transform);
  static Ogre::SceneManager* GetSceneManager(Ogre::Root& ogre_root);
};

}  // namespace z13::ogre
