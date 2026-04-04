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
  static void CreateCamera(flecs::entity e, const gameplay::Camera& camera, OgreData& ogre_data);
  static void UpdateSceneNodeTransform(struct SceneNodeComponent& scene_node_component, const Eigen::Matrix4f& transform);
  static Ogre::SceneManager* GetSceneManager(Ogre::Root& ogre_root);
};

}  // namespace z13::ogre
